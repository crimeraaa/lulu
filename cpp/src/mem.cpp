#include "mem.hpp"
#include "vm.hpp"
#include "compiler.hpp"

#define REPEAT_2(n)   n, n
#define REPEAT_4(n)   REPEAT_2(n), REPEAT_2(n)
#define REPEAT_8(n)   REPEAT_4(n), REPEAT_4(n)
#define REPEAT_16(n)  REPEAT_8(n), REPEAT_8(n)
#define REPEAT_32(n)  REPEAT_16(n), REPEAT_16(n)
#define REPEAT_64(n)  REPEAT_32(n), REPEAT_32(n)
#define REPEAT_128(n) REPEAT_64(n), REPEAT_64(n)

#ifdef LULU_DEBUG_LOG_GC
static int n_calls = 1;
#endif // LULU_DEBUG_LOG_GC

int
mem_ceil_log2(usize n)
{
    lulu_assume(n > 0);

    /**
     * @brief
     *      Map indices in the range [1, 256] to the index range exponents
     *      with which they fit at the end. Useful to determine
     *      the appropriate array size which `n` fits in.
     *
     * @note(2025-08-14)
     *      Index 0 is never a valid input, so this table actually
     *      maps `n - 1`.
     */
    static const u8 ceil_log2_lookup_table[0x100] = {
        // [1, 1] => index_ranges[0]
        0,

        // [2, 2] => index_ranges[1]
        1,

        // [3, 4] => index_ranges[2]
        // Index 3 should map to bit 2 as given by ceil(log2(3)), NOT bit 1
        // as given by floor(log2(3)). Because when we calculate the optimal
        // array size, we want size of 4, not size of 2.
        REPEAT_2(2),
        REPEAT_4(3),   // [5, 8]
        REPEAT_8(4),   // [9, 16]
        REPEAT_16(5),  // [17, 32]
        REPEAT_32(6),  // [33, 64]
        REPEAT_64(7),  // [65, 128]
        REPEAT_128(8), // [129, 256]
    };

    // Accumulator for values of n that do not fit in the lookup table.
    // We know that if it does not fit, 2^8 is automatically added on top.
    // Concept check: ceil(log2(257))
    int acc = 0;
    while (n > 0x100) {
        n >>= 8;
        acc += 8;
    }
    return acc + ceil_log2_lookup_table[n - 1];
}


void *
mem_rawrealloc(lulu_VM *L, void *ptr, usize old_size, usize new_size)
{
    lulu_Global *g = G(L);
    // Collect only if allocating *more* memory (not freeing nor shrinking)
    if (new_size > old_size) {
#ifdef LULU_DEBUG_STRESS_GC
        mem_collect_garbage(L);
#endif
    }

    void *next = g->allocator(g->allocator_data, ptr, old_size, new_size);
    // Allocation request, that wasn't attempting to free, failed?
    if (next == nullptr && new_size != 0) {
        vm_push_string(L, lstring_literal(LULU_MEMORY_ERROR_STRING));
        vm_throw(L, LULU_ERROR_MEMORY);
    }
    return next;
}


/**
 * @note(2025-08-27)
 *      Analogous to `memory.c:markObject()` in Crafting Interpreters 26.3:
 *      Marking the Roots.
 */
static void
mem_mark_object(lulu_Global *g, Object *o)
{
    if (o == nullptr) {
        return;
    }
    // Prevent cycles.
    if (o->base.is_gray()) {
        return;
    }
#ifdef LULU_DEBUG_LOG_GC
    object_gc_print(o, "mark");
#endif // LULU_DEBUG_LOG_GC

    o->base.set_gray();

    GC_List **next;
    switch (o->type()) {
    case VALUE_STRING:
        // Strings can be marked gray, but we do not add them to the gray list
        // because all strings are visible to Intern anyway.
        return;
    case VALUE_TABLE:
        next = &o->table.gc_list;
        break;
    case VALUE_FUNCTION:
        // Also works for C closures
        next = &o->function.lua.gc_list;
        break;
    case VALUE_CHUNK:
        next = &o->chunk.gc_list;
        break;
    default:
        lulu_panicf("Invalid object type %i", o->type());
        lulu_unreachable();
        break;
    }

    GC_List **head;
    switch (g->gc_state) {
    // During mark phase, we can (and should!) fill the primary gray list.
    case GC_MARK:
        head = &g->gray_list;
        break;
    // During trace phase, don't disturb iteration of the main gray lst.
    // We cannot reach here while sweeping!
    case GC_TRACE_SECONDARY:
        // @note(2025-08-29): `o` is some dependent of the object we are
        // about to blacken, so the parent is going to be invalidated.
        //
        // Find the prepended node *before* that so we can link it to the
        // remainder of the list. Remember that unlike in mem_sweep(), we are
        // not necessarily iterating in order of the main objects list!
        if (g->gray_prepend_tail == nullptr) {
            g->gray_prepend_tail = o;
        }
        [[fallthrough]];
    case GC_TRACE_PRIMARY:
        head = &g->gray_saved;
        break;
    default:
        lulu_panicf("Got GC_State %i", g->gc_state);
        lulu_unreachable();
        break;
    }
    // Prepend the current node to the head of parent list.
    *next = *head;
    *head = o;
}


/**
 * @note(2025-08-27)
 *      Analogous to `memory.c:markValue()` in Crafting Interpreters 26.3:
 *      Marking the Roots.
 */
static void
mem_mark_value(lulu_Global *g, Value v)
{
    if (v.is_object()) {
        Object *o = v.to_object();
        mem_mark_object(g, o);
    }
}


static void
mem_mark_array(lulu_Global *g, Slice<Value> a)
{
    for (Value v : a) {
        mem_mark_value(g, v);
    }
}

static GC_List **
mem_blacken_chunk(lulu_Global *g, Chunk *p)
{
    lulu_assert(p->is_gray());
    p->set_black();

    // All local names are not collectible. An interned local identifier
    // may be shared across multiple closures.
    for (Local v : p->locals) {
        mem_mark_object(g, v.ident->to_object());
    }

    // All associated upvalues are not collectible. An upvalue may be shared
    // across multiple closures.
    for (OString *up : p->upvalues) {
        mem_mark_object(g, up->to_object());
    }

    mem_mark_array(g, p->constants);

    // All nested functions are not collectible.
    for (Chunk *f : p->children) {
        mem_mark_object(g, f->to_object());
    }

    mem_mark_object(g, p->source->to_object());
    return &p->gc_list;
}

void
mem_mark_compiler_roots(lulu_VM *L, Compiler *c)
{
    lulu_Global *g = G(L);
    g->gc_state = GC_MARK;
    while (c != nullptr) {
        mem_mark_object(g, c->chunk->to_object());
        mem_mark_object(g, c->indexes->to_object());
        c = c->prev;
    }
}


/**
 * @note(2025-08-27)
 *      Analogous to `memory.c:markTable()` in Crafting Interpreters 26.3:
 *      Marking the Roots.
 */
static GC_List **
mem_blacken_table(lulu_Global *g, Table *t)
{
    lulu_assert(t->is_gray());
    // Table itself should not be collected.
    t->set_black();

    for (Value v : t->array) {
        mem_mark_value(g, v);
    }

    // @todo(2025-08-27) Should this function go in table.cpp?
    for (isize i = 0, n = len(t->entries); i < n; i++) {
        Entry *e = &t->entries[i];
        mem_mark_value(g, e->key);
        mem_mark_value(g, e->value);
    }

    return &t->gc_list;
}

/**
 * @note(2025-08-27)
 *      Analogous to `memory.c:tableRemoveWhite()` in
 *      Crafting Interpreters 26.5.1: Weak references and the string pool.
 */
static void
mem_remove_intern(lulu_VM *L, Intern *t)
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
mem_blacken_upvalue(lulu_Global *g, Upvalue *up)
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
        mem_mark_value(g, up->closed);
    }
    up->set_black();
}

static GC_List **
mem_blacken_function(lulu_Global *g, Closure *f)
{
    lulu_assert(f->lua.is_gray());
    if (f->is_c()) {
        Closure_C *c = f->to_c();
        mem_mark_array(g, c->slice_upvalues());
        c->set_black();
        return &c->gc_list;
    }

    Closure_Lua *lua = f->to_lua();
    mem_mark_object(g, lua->chunk->to_object());
    for (Upvalue *up : lua->slice_upvalues()) {
        mem_blacken_upvalue(g, up);
    }
    lua->set_black();
    return &lua->gc_list;
}

static GC_List *
mem_blacken_object(lulu_Global *g, Object *o)
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
        next = mem_blacken_table(g, &o->table);
        break;
    case VALUE_FUNCTION:
        next = mem_blacken_function(g, &o->function);
        break;
    case VALUE_CHUNK:
        next = mem_blacken_chunk(g, &o->chunk);
        break;
    default:
        lulu_panicf("Cannot blacken object type '%s'", Value::type_names[t]);
        lulu_unreachable();
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
mem_trace_references(lulu_Global *g)
{
    // Traversing through the main list is easy enough.
    g->gc_state = GC_TRACE_PRIMARY;
    while (g->gray_list != nullptr) {
        GC_List *next = mem_blacken_object(g, g->gray_list);
        g->gray_list = next;
    }

    // Traversing the secondary list is harder, because we can prepend to it.
    g->gc_state = GC_TRACE_SECONDARY;
    while (g->gray_saved != nullptr) {
        GC_List *node = g->gray_saved;
        GC_List *next = mem_blacken_object(g, node);
        // No prepending occured, so we can proceed with iteration normally.
        if (g->gray_saved == node) {
            g->gray_saved = next;
            continue;
        }
        // Otherwise, prepending did occur, so don't assign the list because
        // we will traverse that child. We assume the 'tail' of the prepended
        // sequence is the oldest one and comes right before `node`.
        GC_List *last = g->gray_prepend_tail;
        lulu_assert(last != nullptr);
        g->gray_prepend_tail = nullptr;
        last->chunk.gc_list = next;
    }
    // Must be empty by this point, otherwise we will crash during sweep.
    lulu_assert(g->gray_prepend_tail == nullptr);
}


/**
 * @note(2025-08-27)
 *      Analogous to `memory.c:sweep()` in Crafting Interpreters
 *      26.5: Sweeping unused objects.
 */
static void
mem_sweep(lulu_VM *L, lulu_Global *g)
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
mem_mark_roots(lulu_VM *L, lulu_Global *g)
{
    g->gc_state = GC_MARK;
    // Full/active stack.
    Slice<Value> stack = slice_pointer(raw_data(L->stack), vm_top_ptr(L));
    for (Value &v : stack) {
        mem_mark_value(g, v);
    }

    // Pointers to active function objects are also reachable.
    for (Call_Frame &cf : small_array_slice(L->frames)) {
        mem_mark_object(g, reinterpret_cast<Object *>(cf.function));
    }

    // All open upvalues are also reachable.
    for (Object *o = L->open_upvalues; o != nullptr; o = o->next()) {
        mem_blacken_upvalue(g, &o->upvalue);
    }

    // Globals table is always reachable, save it for later when tracing.
    // We should not reach this point at VM startup.
    mem_mark_value(g, L->globals);
}

void
mem_collect_garbage(lulu_VM *L)
{
    lulu_Global *g = G(L);
    if (g->gc_state == GC_PAUSED) {
        return;
    }

#ifdef LULU_DEBUG_LOG_GC
    printf("--- gc begin (%i)\n", n_calls);
#endif

    mem_mark_roots(L, g);
    mem_trace_references(g);
    mem_remove_intern(L, &g->intern);
    mem_sweep(L, g);

#ifdef LULU_DEBUG_LOG_GC
    printf("--- gc end (%i)\n", n_calls);
    n_calls++;
#endif
}
