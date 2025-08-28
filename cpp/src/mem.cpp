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

static void
mem_mark_array(lulu_VM *L, Slice<Value> a)
{
    for (Value v : a) {
        mem_mark_value(L, v);
    }
}

static void
mem_mark_chunk(lulu_VM *L, Chunk *p)
{
    if (p->is_black()) {
        return;
    }
    p->set_black();

    // All local names are not collectible. An interned local identifier
    // may be shared across multiple closures.
    for (Local v : p->locals) {
        mem_mark_object(L, v.ident->to_object());
    }

    // All associated upvalues are not collectible. An upvalue may be shared
    // across multiple closures.
    for (OString *up : p->upvalues) {
        mem_mark_object(L, up->to_object());
    }

    mem_mark_array(L, p->constants);

    // All nested functions are not collectible.
    for (Chunk *f : p->children) {
        mem_mark_object(L, f->to_object());
    }

    mem_mark_object(L, p->source->to_object());
}

void
mem_mark_compiler_roots(lulu_VM *L, Compiler *c)
{
    while (c != nullptr) {
        mem_mark_chunk(L, c->chunk);
        mem_mark_object(L, c->indexes->to_object());
        c = c->prev;
    }
}

void
mem_mark_table(lulu_VM *L, Table *t)
{
    // Already traversed so running below code would be redundant.
    if (t->is_black()) {
        return;
    }

    // Table itself should not be collected.
    t->set_black();

    for (Value v : t->array) {
        mem_mark_value(L, v);
    }

    // @todo(2025-08-27) Should this function go in table.cpp?
    for (isize i = 0, n = len(t->entries); i < n; i++) {
        Entry *e = &t->entries[i];
        mem_mark_value(L, e->key);
        mem_mark_value(L, e->value);
    }
}

void
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

// Assumes that all black objects also have the gray bit toggled.
void
mem_mark_object(lulu_VM *L, Object *o)
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
    cdynamic_push(&L->gray_stack, o);
}

void
mem_mark_value(lulu_VM *L, Value v)
{
    if (v.is_object()) {
        mem_mark_object(L, v.to_object());
    }
}

static void
mem_mark_function(lulu_VM *L, Closure *f)
{
    if (f->is_c()) {
        Closure_C *c = f->to_c();
        mem_mark_array(L, c->slice_upvalues());
        c->set_black();
        return;
    }

    Closure_Lua *lua = f->to_lua();
    mem_mark_object(L, lua->chunk->to_object());
    for (Upvalue *uv : lua->slice_upvalues()) {
        mem_mark_object(L, uv->to_object());
    }
    lua->set_black();
}

static void
mem_blacken_object(lulu_VM *L, Object *o)
{
    Value_Type t = o->type();
#ifdef LULU_DEBUG_LOG_GC
    object_gc_print(o, "blacken");
#endif // LULU_DEBUG_LOG_GC

    switch (t) {
    case VALUE_STRING:
        // Although active strings in the stack are already marked, strings
        // from tables/chunks are not.
        mem_mark_object(L, o);
        break;
    case VALUE_TABLE:
        mem_mark_table(L, &o->table);
        break;
    case VALUE_FUNCTION:
        mem_mark_function(L, &o->function);
        break;
    case VALUE_CHUNK:
        mem_mark_chunk(L, &o->chunk);
        break;
    case VALUE_UPVALUE: {
        Upvalue *up = &o->upvalue;
        // Closed is only our child when we are, well, closed.
        // When open the value lives on the stack we the GC took care of it.
        if (up->value == &up->closed) {
            mem_mark_value(L, up->closed);
        }
        up->set_black();
        break;
    }
    default:
        lulu_panicf("Cannot blacken object type '%s'", Value::type_names[t]);
        lulu_unreachable();
        break;
    }
}


/**
 * @note(2025-08-27)
 *      Analogous to `memory.c:traceReferences()` in Crafting Interpreters
 *      26.4.3: Processing gray objects.
 */
static void
mem_trace_references(lulu_VM *L)
{
    while (L->gray_stack.len > 0) {
        Object *o = cdynamic_pop(&L->gray_stack);
        mem_blacken_object(L, o);
    }
}


/**
 * @note(2025-08-27)
 *      Analogous to `memory.c:sweep()` in Crafting Interpreters
 *      26.5: Sweeping unused objects.
 */
static void
mem_sweep(lulu_VM *L)
{
    lulu_Global *g = G(L);

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
        // `o` is not marked (white).
        Object *unreached = o;
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
mem_mark_roots(lulu_VM *L)
{
    // Full/active stack.
    Slice<Value> stack = slice_pointer(raw_data(L->stack), vm_top_ptr(L));
    for (Value &v : stack) {
        mem_mark_value(L, v);
    }

    // Pointers to active function objects are also reachable.
    for (Call_Frame &cf : small_array_slice(L->frames)) {
        mem_mark_object(L, reinterpret_cast<Object *>(cf.function));
    }

    // All open upvalues are also reachable.
    for (Object *o = L->open_upvalues; o != nullptr; o = o->next()) {
        mem_mark_object(L, o);
    }

    // Globals table is always reachable, save it for later when tracing.
    // We should not reach this point at VM startup.
    mem_mark_value(L, L->globals);
}

void
mem_collect_garbage(lulu_VM *L)
{
    lulu_Global *g = G(L);
    if (g->gc_paused) {
        return;
    }


#ifdef LULU_DEBUG_LOG_GC
    static int n_calls = 1;
    printf("--- gc begin (%i)\n", n_calls);
#endif

    mem_mark_roots(L);
    mem_trace_references(L);
    mem_remove_intern(L, &g->intern);
    mem_sweep(L);

#ifdef LULU_DEBUG_LOG_GC
    printf("--- gc end (%i)\n", n_calls);
    n_calls++;
#endif
}
