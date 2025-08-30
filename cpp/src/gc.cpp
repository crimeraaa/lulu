#include "gc.hpp"
#include "vm.hpp"
#include "compiler.hpp"

#ifdef LULU_DEBUG_LOG_GC
static int n_calls = 1;
#endif // LULU_DEBUG_LOG_GC

/**
 * @note(2025-08-27)
 *      Analogous to `memory.c:markObject()` in Crafting Interpreters 26.3:
 *      Marking the Roots.
 */
static void
gc_mark_object(lulu_Global *g, Object *o)
{
    if (o == nullptr) {
        return;
    }

    // Skip if gray OR black
    if (!o->base.is_white()) {
        return;
    }

#ifdef LULU_DEBUG_LOG_GC
    object_gc_print(o, "mark");
#endif // LULU_DEBUG_LOG_GC

    o->base.set_gray_from_white();

    // During mark phase, this node will link to the current head as we
    // are going to prepend it, thus making it the new head.
    //
    // During trace phase, this node will link to nothing as we are going
    // to append it, thus making it the new tail.
    GC_List *next = (g->gc_state == GC_MARK) ? g->gray_head : nullptr;
    switch (o->type()) {
    case VALUE_STRING:
        // Strings can be marked gray, but we do not add them to the gray list
        // because all strings are visible to Intern anyway.
        return;
    case VALUE_TABLE:
        o->table.gc_list = next;
        break;
    case VALUE_FUNCTION:
        o->function.base.gc_list = next;
        break;
    case VALUE_CHUNK:
        o->chunk.gc_list = next;
        break;
    default:
        lulu_panicf("Object '%s' has no member 'gc_list'", o->type_name());
        break;
    }

    switch (g->gc_state) {
    case GC_MARK:
        // First node ever? Will be used if/when we append later.
        if (g->gray_tail == nullptr) {
            g->gray_tail = g->gray_head;
        }
        // Node was linked to current head, so head will now be the node
        // in order to prepend it.
        g->gray_head = o;
        break;
    case GC_TRACE:
        lulu_assert(g->gray_tail != nullptr);
        // Update current tail to link to new node...
        switch (g->gray_tail->type()) {
        case VALUE_TABLE:
            g->gray_tail->table.gc_list = o;
            break;
        case VALUE_FUNCTION:
            g->gray_tail->function.base.gc_list = o;
            break;
        case VALUE_CHUNK:
            g->gray_tail->chunk.gc_list = o;
            break;
        default:
            lulu_panicf("Object '%s' has no member 'gc_list'",
                g->gray_tail->type_name());
            break;
        }
        // ...then set new node as the tail.
        g->gray_tail = o;
        break;
    default:
        lulu_panicf("Got GC_State %i", g->gc_state);
        break;
    }
}


/**
 * @note(2025-08-27)
 *      Analogous to `memory.c:markValue()` in Crafting Interpreters 26.3:
 *      Marking the Roots.
 */
static void
gc_mark_value(lulu_Global *g, Value v)
{
    if (v.is_object()) {
        Object *o = v.to_object();
        gc_mark_object(g, o);
    }
}


static void
gc_mark_array(lulu_Global *g, Slice<Value> a)
{
    for (Value v : a) {
        gc_mark_value(g, v);
    }
}

static GC_List **
gc_blacken_chunk(lulu_Global *g, Chunk *p)
{
    lulu_assert(p->is_gray());
    p->set_black();

    // All local names are not collectible. An interned local identifier
    // may be shared across multiple closures.
    for (Local v : p->locals) {
        gc_mark_object(g, v.ident->to_object());
    }

    // All associated upvalues are not collectible. An upvalue may be shared
    // across multiple closures.
    for (OString *up : p->upvalues) {
        gc_mark_object(g, up->to_object());
    }

    gc_mark_array(g, p->constants);

    // All nested functions are not collectible.
    for (Chunk *f : p->children) {
        gc_mark_object(g, f->to_object());
    }

    gc_mark_object(g, p->source->to_object());
    return &p->gc_list;
}

void
gc_mark_compiler_roots(lulu_VM *L, Compiler *c)
{
    lulu_Global *g = G(L);
    g->gc_state = GC_MARK;
    while (c != nullptr) {
        gc_mark_object(g, c->chunk->to_object());
        gc_mark_object(g, c->indexes->to_object());
        c = c->prev;
    }
    g->gc_state = GC_PAUSED;
}


/**
 * @note(2025-08-27)
 *      Analogous to `memory.c:markTable()` in Crafting Interpreters 26.3:
 *      Marking the Roots.
 */
static GC_List **
gc_blacken_table(lulu_Global *g, Table *t)
{
    lulu_assert(t->is_gray());
    // Table itself should not be collected.
    t->set_black();

    for (Value v : t->array) {
        gc_mark_value(g, v);
    }

    // @todo(2025-08-27) Should this function go in table.cpp?
    for (isize i = 0, n = len(t->entries); i < n; i++) {
        Entry *e = &t->entries[i];
        gc_mark_value(g, e->key);
        gc_mark_value(g, e->value);
    }

    return &t->gc_list;
}


/**
 * @note(2025-08-27)
 *      Analogous to `memory.c:tableRemoveWhite()` in
 *      Crafting Interpreters 26.5.1: Weak references and the string pool.
 */
static void
gc_remove_intern(lulu_VM *L, Intern *t)
{
    for (Object *&o : t->table) {
        // Since strings are kept in their own lists, we can free them
        // directly.
        Object *prev = nullptr;
        Object *it = o;
        while (it != nullptr) {
            OString *s = &it->ostring;

            // Save now in case `s` is freed.
            Object *next = s->next;

            // Previously marked (in stack, etc.) or is a keyword?
            if (s->is_gray() || s->is_fixed()) {
                s->set_white();
                prev = it;
            } else {
                if (prev != nullptr) {
                    // Unlink from middle of list.
                    prev->base.next = next;
                } else {
                    // Unlink from primary array slot (the head).
                    o = next;
                }
                object_free(L, it);
            }
            it = next;
        }
    }
}

// Upvalues cannot (and should not) be marked gray at any point. They go
// directly to black because they have no dependents other than their
// pointed-to value.
static void
gc_blacken_upvalue(lulu_Global *g, Upvalue *up)
{
    // @note(2025-08-29) Can occur if we collect garbage right after
    // creating a closure with nonzero upvalues but before actually
    // creating any of them.
    if (up == nullptr) {
        return;
    }

    // Since multiple closures can share the same upvalue, we may visit this
    // multiple times.
    if (up->is_black()) {
        return;
    }

    // Closed is only our child when we are, well, closed.
    // When open the value lives on the stack we the GC took care of it.
    if (up->value == &up->closed) {
        gc_mark_value(g, up->closed);
    }
    up->set_black();
}

static GC_List **
gc_blacken_function(lulu_Global *g, Closure *f)
{
    lulu_assert(f->lua.is_gray());
    if (f->is_c()) {
        Closure_C *c = f->to_c();
        gc_mark_array(g, c->slice_upvalues());
        c->set_black();
        return &c->gc_list;
    }

    Closure_Lua *lua = f->to_lua();
    gc_mark_object(g, lua->chunk->to_object());
    for (Upvalue *up : lua->slice_upvalues()) {
        gc_blacken_upvalue(g, up);
    }
    lua->set_black();
    return &lua->gc_list;
}

static GC_List *
gc_blacken_object(lulu_Global *g, Object *o)
{
    Value_Type t = o->type();
    // If an object was already black, then it should not have been added to
    // the either working list.
    lulu_assert(o->base.is_gray());

#ifdef LULU_DEBUG_LOG_GC
    object_gc_print(o, "blacken");
#endif // LULU_DEBUG_LOG_GC

    GC_List **next;
    switch (t) {
    case VALUE_TABLE:
        next = gc_blacken_table(g, &o->table);
        break;
    case VALUE_FUNCTION:
        next = gc_blacken_function(g, &o->function);
        break;
    case VALUE_CHUNK:
        next = gc_blacken_chunk(g, &o->chunk);
        break;
    default:
        lulu_panicf("Cannot blacken object type '%s'", o->type_name());
        break;
    }
    lulu_assert(o->base.is_black());
    GC_List *saved = *next;
    // Unlink this object from the gray list.
    *next = nullptr;
    return saved;
}


/**
 * @note(2025-08-27)
 *      Analogous to `memory.c:traceReferences()` in Crafting Interpreters
 *      26.4.3: Processing gray objects.
 */
static void
gc_trace_references(lulu_Global *g)
{
    g->gc_state = GC_TRACE;

    // While traversing, we may append new objects. This is fine because
    // since they're appended, we have not invalidated the iteration.
    while (g->gray_head != nullptr) {
        GC_List *next = gc_blacken_object(g, g->gray_head);
        g->gray_head = next;
    }

    // Prepare for next cycle
    g->gray_tail = nullptr;
}


/**
 * @note(2025-08-27)
 *      Analogous to `memory.c:sweep()` in Crafting Interpreters
 *      26.5: Sweeping unused objects.
 */
static void
gc_sweep(lulu_VM *L, lulu_Global *g)
{
    g->gc_state = GC_SWEEP;

    // Track the parent list to unlink.
    Object *prev = nullptr;
    Object *o = g->objects;
    while (o != nullptr) {
        Object *next = o->next();
        // If marked (black), continue past.
        if (o->base.is_black()) {
            // Prepare all black objects for the next cycle by setting them
            // to white.
            o->base.set_white();

            // We may unlink an unreachable object from this one.
            prev = o;
            o = next;
            continue;
        }
        Object *unreached = o;
        // If the object is still gray, then that means we messed up somewhere
        // as we failed to traverse it.
        lulu_assert(!o->base.is_gray());

        // Unlink the unreached object from its parent linked list right
        // before we free it.
        if (prev != nullptr) {
            prev->base.next = next;
        } else {
            g->objects = next;
        }
        o = next;
        object_free(L, unreached);
    }
}

static void
gc_mark_roots(lulu_VM *L, lulu_Global *g)
{
    g->gc_state = GC_MARK;
    // Full/active stack.
    Slice<Value> stack = slice_pointer(raw_data(L->stack), vm_top_ptr(L));
    for (Value &v : stack) {
        gc_mark_value(g, v);
    }

    // Pointers to active function objects are also reachable.
    for (Call_Frame &cf : small_array_slice(L->frames)) {
        gc_mark_object(g, reinterpret_cast<Object *>(cf.function));
    }

    // All open upvalues are also reachable.
    // @note(2025-08-30) Perhaps this is redunant? Upvalues in the open upvalue
    // list are not reachable by the main objects list, so they cannot be
    // freed anyway?
    for (Object *o = L->open_upvalues; o != nullptr; o = o->next()) {
        gc_blacken_upvalue(g, &o->upvalue);
    }

    // Globals table is always reachable, save it for later when tracing.
    // We should not reach this point at VM startup.
    gc_mark_value(g, L->globals);
}

void
gc_collect_garbage(lulu_VM *L, lulu_Global *g)
{
#ifdef LULU_DEBUG_LOG_GC
    printf("--- gc begin (%i)\n", n_calls);
    usize before = g->n_bytes_allocated;
#endif

    gc_mark_roots(L, g);
    gc_trace_references(g);
    gc_remove_intern(L, &g->intern);
    gc_sweep(L, g);
    g->gc_threshold = g->n_bytes_allocated * GC_HEAP_GROW_FACTOR;
    g->gc_state = GC_PAUSED;

#ifdef LULU_DEBUG_LOG_GC
    printf("--- gc end (%i)\n", n_calls);
    printf("    collected %zu bytes (from %zu to %zu), next GC at %zu\n",
        before - g->n_bytes_allocated, before, g->n_bytes_allocated,
        g->gc_threshold);
    n_calls++;
#endif
}
