#include "stream.hpp"
#include "vm.hpp"

const static Value *
value_at(lulu_VM *L, int i)
{
    // Resolve 1-based relative index.
    isize ii = i;
    if (ii > 0) {
        ii--;
        if (0 <= ii && ii < len(L->window)) {
            return &L->window[ii];
        }
        return &nil;
    } else if (ii > LULU_PSEUDO_INDEX) {
        // Not valid in any way
        lulu_assert(ii != 0);
        lulu_assert(0 <= len(L->window) + ii);
        return &L->window[len(L->window) + ii];
    }

    // Not in range of the window; try a pseudo index.
    switch (ii) {
    case LULU_GLOBALS_INDEX:
        return &L->globals;
    default:
        break;
    }

    // Must be an upvalue; try to resolve it.
    Closure_C *f = L->caller->to_c();

    // Undo the pseudo index offset to get the actual upvalue index.
    int up_i = (LULU_GLOBALS_INDEX - ii) - 1;
    if (0 <= up_i && up_i < f->n_upvalues) {
        return &f->upvalues[up_i];
    }
    return &nil;
}

static Value *
value_at_stack(lulu_VM *L, int i)
{
    const Value *start = value_at(L, i);
    lulu_assert(start != &nil);
    lulu_assertf(i > LULU_PSEUDO_INDEX, "Got pseudo-index %i", i);

    // Only `&nil` is immutable; all indexes (even pseudo indexes!)
    // are mutable.
    return const_cast<Value *>(start);
}

LULU_API lulu_CFunction
lulu_set_panic(lulu_VM *L, lulu_CFunction panic_fn)
{
    lulu_CFunction prev = G(L)->panic_fn;
    G(L)->panic_fn = panic_fn;
    return prev;
}

LULU_API lulu_Error
lulu_load(lulu_VM *L, const char *source, lulu_Reader reader,
    void *reader_data)
{
    Stream stream{};
    stream.function = reader;
    stream.data = reader_data;
    lulu_Error e = vm_load(L, lstring_from_cstring(source), &stream);
    return e;
}

LULU_API void
lulu_call(lulu_VM *L, int n_args, int n_rets)
{
    const Value *fn = value_at(L, -(n_args + 1));
    vm_call(L, fn, n_args, (n_rets == LULU_MULTRET) ? VARARG : n_rets);
}

struct PCall {
    int n_args, n_rets;
};

static void
pcall(lulu_VM *L, void *user_ptr)
{
    PCall *d = reinterpret_cast<PCall *>(user_ptr);
    lulu_call(L, d->n_args, d->n_rets);
}

LULU_API lulu_Error
lulu_pcall(lulu_VM *L, int n_args, int n_rets)
{
    PCall d{n_args, n_rets};
    return vm_pcall(L, pcall, &d);
}

struct CPCall {
    lulu_CFunction function;
    void          *function_data;
};

static void
cpcall(lulu_VM *L, void *user_ptr)
{
    CPCall *d = reinterpret_cast<CPCall *>(user_ptr);
    lulu_push_cfunction(L, d->function);
    lulu_push_userdata(L, d->function_data);
    lulu_call(L, 1, 0);
}

LULU_API lulu_Error
lulu_cpcall(lulu_VM *L, lulu_CFunction function, void *function_data)
{
    CPCall d{function, function_data};
    lulu_Error e = vm_pcall(L, cpcall, &d);
    return e;
}

LULU_API int
lulu_error(lulu_VM *L)
{
    vm_throw(L, LULU_ERROR_RUNTIME);
    return 0;
}

/*=== TYPE QUERY FUNCTIONS ========================================== {{{ */

LULU_API lulu_Type
lulu_type(lulu_VM *L, int i)
{
    const Value *v = value_at(L, i);
    if (v == &nil) {
        return LULU_TYPE_NONE;
    }

    Value_Type t = v->type();
    lulu_assertf(VALUE_NIL <= t && t <= VALUE_TYPE_LAST,
        "Got Value_Type(%i)", t);
    return static_cast<lulu_Type>(t);
}

LULU_API const char *
lulu_type_name(lulu_VM *L, lulu_Type t)
{
    unused(L);
    return (t == LULU_TYPE_NONE) ? "no value" : Value::type_names[t];
}

LULU_API int
lulu_is_number(lulu_VM *L, int i)
{
    const Value *v = value_at(L, i);
    Value        tmp;
    return vm_to_number(v, &tmp);
}

LULU_API int
lulu_is_string(lulu_VM *L, int i)
{
    // `number` is always convertible to a string.
    Value_Type t = value_at(L, i)->type();
    return t == VALUE_NUMBER || t == VALUE_STRING;
}

/*=== }}} =============================================================== */

/*=== STACK MANIPULATION FUNCTIONS ================================== {{{ */

LULU_API int
lulu_to_boolean(lulu_VM *L, int i)
{
    return !value_at(L, i)->is_falsy();
}

LULU_API lulu_Number
lulu_to_number(lulu_VM *L, int i)
{
    Value        tmp;
    const Value *v = value_at(L, i);
    if (vm_to_number(v, &tmp)) {
        return tmp.to_number();
    }
    return 0;
}

LULU_API lulu_Integer
lulu_to_integer(lulu_VM *L, int i)
{
    return static_cast<Integer>(lulu_to_number(L, i));
}

LULU_API const char *
lulu_to_lstring(lulu_VM *L, int i, size_t *n)
{
    const Value *v = value_at(L, i);

    /**
     * @note(2025-08-02)
     *      This call is safe, because if `v == &nil`, it has the
     *      `nil` tag and nothing is changed.
     */
    if (!vm_to_string(L, const_cast<Value *>(v))) {
        if (n != nullptr) {
            *n = 0;
        }
        return nullptr;
    }
    // gc_check(L); // luaC_checkGC(L);

    // Otherwise, conversion success.
    // @note(2025-08-30) Previous call may reallocate stack in the future!
    OString *s = v->to_ostring();
    if (n != nullptr) {
        *n = static_cast<usize>(s->len);
    }
    return s->to_cstring();
}

LULU_API void *
lulu_to_pointer(lulu_VM *L, int i)
{
    return value_at(L, i)->to_pointer();
}

LULU_API int
lulu_get_top(lulu_VM *L)
{
    return len(L->window);
}

LULU_API void
lulu_set_top(lulu_VM *L, int i)
{
    if (i >= 0) {
        int old_start = ptr_index(L->stack, raw_data(L->window));
        int old_stop  = old_start + static_cast<int>(len(L->window));
        int new_stop  = old_start + i;
        if (new_stop > old_stop) {
            // If growing the window, initialize the new region to nil.
            Slice<Value> extra = slice(L->stack, old_stop, new_stop);
            fill(extra, nil);
        }
        L->window = slice(L->stack, old_start, new_stop);
    } else {
        lulu_assert(value_at_stack(L, i));
        lulu_pop(L, -i);
    }
}

LULU_API void
lulu_insert(lulu_VM *L, int i)
{
    Value *start = value_at_stack(L, i);

    // Copy by value as this stack slot is about to be replaced.
    Value v   = *value_at(L, -1);
    auto  dst = slice_pointer(start + 1, end(L->window));
    auto  src = slice_pointer_len(start, len(dst));
    copy(dst, src);
    *start = v;
}

LULU_API void
lulu_remove(lulu_VM *L, int i)
{
    Value *start = value_at_stack(L, i);
    Value *stop  = value_at_stack(L, -1);
    auto   dst   = slice_pointer_len(start, stop - start);
    auto   src   = slice_pointer_len(start + 1, len(dst));
    copy(dst, src);
    lulu_pop(L, 1);
}

LULU_API void
lulu_pop(lulu_VM *L, int n)
{
    isize i    = len(L->window) - n;
    L->window = slice_until(L->window, i);
}

LULU_API void
lulu_push_nil(lulu_VM *L)
{
    vm_push_value(L, nil);
}

LULU_API void
lulu_push_boolean(lulu_VM *L, int b)
{
    vm_push_value(L, Value::make_boolean(b));
}

LULU_API void
lulu_push_number(lulu_VM *L, lulu_Number n)
{
    vm_push_value(L, Value::make_number(n));
}

LULU_API void
lulu_push_integer(lulu_VM *L, lulu_Integer i)
{
    vm_push_value(L, Value::make_number(static_cast<Number>(i)));
}

LULU_API void
lulu_push_userdata(lulu_VM *L, void *p)
{
    vm_push_value(L, Value::make_userdata(p));
}

LULU_API void
lulu_push_lstring(lulu_VM *L, const char *s, size_t n)
{
    // gc_check(L); // luaC_checkGC(L);
    LString ls{s, static_cast<isize>(n)};
    OString *os = ostring_new(L, ls);
    vm_push_value(L, Value::make_string(os));
}

LULU_API void
lulu_push_string(lulu_VM *L, const char *s)
{
    if (s == nullptr) {
        lulu_push_nil(L);
    } else {
        lulu_push_lstring(L, s, strlen(s));
    }
}

LULU_API const char *LULU_ATTR_PRINTF(2, 3)
lulu_push_fstring(lulu_VM *L, const char *fmt, ...)
{
    va_list args;
    // gc_check(L); // luaC_checkGC(L);
    va_start(args, fmt);
    const char *s = vm_push_vfstring(L, fmt, args);
    va_end(args);
    return s;
}

LULU_API const char *
lulu_push_vfstring(lulu_VM *L, const char *fmt, va_list args)
{
    // gc_check(L); // luaC_checkGC(L);
    return vm_push_vfstring(L, fmt, args);
}

LULU_API void
lulu_push_cclosure(lulu_VM *L, lulu_CFunction cf, int n_upvalues)
{
    lulu_assert(n_upvalues >= 0);

    // gc_check(L); // luaC_checkGC(L);
    // api_checknelems(L, n_upvalues);
    Closure *f = closure_c_new(L, cf, n_upvalues);
    for (int i = 0; i < n_upvalues; i++) {
        Value v = *value_at_stack(L, -n_upvalues + i);
        f->c.upvalues[i] = v;
    }
    lulu_pop(L, n_upvalues);
    vm_push_value(L, Value::make_function(f));
}

LULU_API void
lulu_push_value(lulu_VM *L, int i)
{
    const Value *v = value_at(L, i);
    vm_push_value(L, *v);
}

/*=== }}} =============================================================== */

LULU_API void
lulu_new_table(lulu_VM *L, int n_array, int n_hash)
{
    // gc_check(L); // luaC_checkGC(L);
    Table *t = table_new(L, n_array, n_hash);
    vm_push_value(L, Value::make_table(t));
}

LULU_API int
lulu_get_table(lulu_VM *L, int table_index)
{
    const Value *t = value_at(L, table_index);
    Value *k = value_at_stack(L, -1);
    // No need to push, `k` can be overwritten in-place safely.
    return vm_table_get(L, t, *k, k);
}

LULU_API int
lulu_get_field(lulu_VM *L, int table_index, const char *key)
{
    const Value *t = value_at(L, table_index);

    // Unlike `lulu_get_table()`, we need to explicitly push `t[k]` because
    // `k` does not yet exist in the stack; it may otherwise be collected.
    OString *s = ostring_from_cstring(L, key);

    // Must be pushed to stack to prevent an early collection.
    const Value k = t->make_string(s);
    vm_push_value(L, k);
    Value v;
    bool ok = vm_table_get(L, t, k, &v);

    // Replace k with v; net stack change is 0.
    vm_pop_value(L);
    vm_push_value(L, v);
    return ok;
}

LULU_API void
lulu_set_table(lulu_VM *L, int table_index)
{
    const Value *t = value_at(L, table_index);
    const Value *k = value_at(L, -2);
    const Value *v = value_at(L, -1);
    vm_table_set(L, t, k, *v);
    lulu_pop(L, 2);
}

LULU_API void
lulu_set_field(lulu_VM *L, int table_index, const char *key)
{
    const Value *t = value_at(L, table_index);
    OString *s = ostring_from_cstring(L, key);
    // Must be pushed to stack to prevent early garbage collection.
    const Value k = Value::make_string(s);
    Value v = *value_at(L, -1);
    vm_push_value(L, k);
    vm_table_set(L, t, &k, v);
    vm_pop_value(L);
    vm_pop_value(L);
}

LULU_API int
lulu_next(lulu_VM *L, int table_index)
{
    Table *t = value_at(L, table_index)->to_table();
    Value *k = value_at_stack(L, -1);
    Value  v;
    bool   more = table_next(L, t, k, &v);
    if (more) {
        vm_push_value(L, v);
    } else {
        // No more elements, remove the key.
        vm_pop_value(L);
    }
    return static_cast<int>(more);
}

LULU_API size_t
lulu_obj_len(lulu_VM *L, int i)
{
    const Value *v = value_at(L, i);
    if (v->is_string()) {
        return v->to_ostring()->len;
    }
    return 0;
}

LULU_API void
lulu_concat(lulu_VM *L, int n)
{
    switch (n) {
    case 0:
        lulu_push_literal(L, "");
        return;
    case 1:
        return; // Nothing we can sensibly do, other than conversion.
    }

    // api_checknelems(L, n);
    lulu_assert(2 <= n && n <= len(L->window));
    Value *first = value_at_stack(L, -n);
    Value *last  = value_at_stack(L, -1);

    // gc_check(L); // luaC_checkGC(L);
    vm_concat(L, first, slice_pointer(first, last + 1));

    // Pop all arguments except the first one- the one we replaced.
    lulu_pop(L, n - 1);
}

LULU_API int
lulu_gc(lulu_VM *L, lulu_GC_Mode mode)
{
    lulu_Global *g = G(L);
    int n = 0;
    switch (mode) {
    case LULU_GC_STOP:
        g->gc_prev_threshold = g->gc_threshold;
        // We assume that we can never validly request nor acquire this much
        // memory, so it is never reached and thus the GC is never run.
        g->gc_threshold = USIZE_MAX;
        break;
    case LULU_GC_RESTART:
        g->gc_threshold = g->gc_prev_threshold;
        break;
    case LULU_GC_COUNT:
        // Divide by GC_KILOBYTE, optimizing for power-of-2 math.
        n = static_cast<int>(g->n_bytes_allocated >> GC_KILOBYTE_EXP);
        break;
    case LULU_GC_COUNT_REM:
        // Remainder by GC_KILOBYTE, optimizing for power-of-2 math.
        n = static_cast<int>(g->n_bytes_allocated & (GC_KILOBYTE - 1));
        break;
    case LULU_GC_COLLECT:
        gc_collect_garbage(L, g);
        break;
    default:
        n = -1;
        break;
    }
    return n;
}
