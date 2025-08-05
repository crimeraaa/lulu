#include "vm.hpp"
#include "stream.hpp"

// Do not make `constexpr`; must have an address in order to be a reference.
static const Value
none = nil;

const static Value *
value_at(lulu_VM *vm, int i)
{
    // Resolve 1-based relative index.
    isize ii = cast_isize(i);
    if (ii > 0) {
        ii--;
        if (0 <= ii && ii < len(vm->window)) {
            return &vm->window[ii];
        }
        return &none;
    } else if (ii > LULU_PSEUDO_INDEX) {
        // Not valid in any way
        lulu_assert(ii != 0);
        lulu_assert(0 <= len(vm->window) + ii);
        return &vm->window[len(vm->window) + ii];
    }

    // Not in range of the window; try a pseudo index.
    switch (ii) {
    case LULU_GLOBALS_INDEX:
        return &vm->globals;
    default:
        break;
    }

    // Must be an upvalue; try to resolve it.
    Closure_C *f = vm->caller->to_c();

    // Undo the pseudo index offset to get the actual upvalue index.
    int up_i = (LULU_GLOBALS_INDEX - ii) - 1;
    if (0 <= up_i && up_i < f->n_upvalues) {
        return &f->upvalues[up_i];
    }
    return &none;
}

static Value *
value_at_stack(lulu_VM *vm, int i)
{
    const Value *start = value_at(vm, i);
    lulu_assert(start != &none);
    lulu_assertf(i > LULU_PSEUDO_INDEX, "Got pseudo-index %i", i);

    // Only `api.cpp:none` is immutable; all indexes (even pseudo indexes!)
    // are mutable.
    return const_cast<Value *>(start);
}

static void
required_allocations(lulu_VM *vm, void *)
{
    // Ensure when we start interning strings we can already index.
    intern_resize(vm, &vm->intern, 32);

    // TODO(2025-06-30): Mark the memory error string as "immortal"?
    OString *o = ostring_new(vm, lstring_literal(LULU_MEMORY_ERROR_STRING));
    unused(o);
    Table *t = table_new(vm, /* n_hash */ 8, /* n_array */ 0);
    vm->globals.set_table(t);
}

LULU_API lulu_VM *
lulu_open(lulu_Allocator allocator, void *allocator_data)
{
    static lulu_VM _vm;

    lulu_VM *vm = &_vm;
    small_array_clear(&vm->frames);

    vm->panic_fn       = nullptr;
    vm->allocator      = allocator;
    vm->allocator_data = allocator_data;
    vm->saved_ip       = nullptr;
    vm->objects        = nullptr;
    vm->window         = slice(vm->stack, 0, 0);
    vm->globals        = nil;

    builder_init(&vm->builder);
    intern_init(&vm->intern);
    Error e = vm_run_protected(vm, required_allocations, nullptr);
    if (e != LULU_OK) {
        lulu_close(vm);
        return nullptr;
    }
    return vm;
}

LULU_API lulu_C_Function
lulu_set_panic(lulu_VM *vm, lulu_C_Function panic_fn)
{
    lulu_C_Function prev = vm->panic_fn;
    vm->panic_fn = panic_fn;
    return prev;
}

LULU_API void
lulu_close(lulu_VM *vm)
{
    builder_destroy(vm, &vm->builder);
    intern_destroy(vm, &vm->intern);

    Object *o = vm->objects;
    while (o != nullptr) {
        // Save because `o` is about to be invalidated.
        Object *next = o->base.next;
        object_free(vm, o);
        o = next;
    }
}

LULU_API lulu_Error
lulu_load(lulu_VM *vm, const char *source, lulu_Reader reader,
    void *reader_data)
{
    Stream stream;
    stream.function  = reader;
    stream.data      = reader_data;
    stream.cursor    = nullptr;
    stream.remaining = 0;

    lulu_Error e = vm_load(vm, lstring_from_cstring(source), &stream);
    return e;
}

LULU_API void
lulu_call(lulu_VM *vm, int n_args, int n_rets)
{
    const Value *fn = value_at(vm, -(n_args + 1));
    vm_call(vm, fn, n_args, (n_rets == LULU_MULTRET) ? VARARG : n_rets);
}

struct LULU_PRIVATE PCall {
    int n_args, n_rets;
};

static void
pcall(lulu_VM *vm, void *user_ptr)
{
    PCall *d = reinterpret_cast<PCall *>(user_ptr);
    const Value *fn = value_at(vm, -(d->n_args + 1));
    vm_call(vm, fn, d->n_args, (d->n_rets == LULU_MULTRET) ? VARARG : d->n_rets);
}

LULU_API lulu_Error
lulu_pcall(lulu_VM *vm, int n_args, int n_rets)
{
    PCall d{n_args, n_rets};
    lulu_Error e = vm_run_protected(vm, pcall, &d);
    return e;
}

struct LULU_PRIVATE C_PCall {
    lulu_C_Function function;
    void          *function_data;
};

static void
c_pcall(lulu_VM *vm, void *user_ptr)
{
    C_PCall *d = reinterpret_cast<C_PCall *>(user_ptr);
    lulu_push_c_function(vm, d->function);
    lulu_push_userdata(vm, d->function_data);
    lulu_call(vm, 1, 0);
}

LULU_API lulu_Error
lulu_c_pcall(lulu_VM *vm, lulu_C_Function function, void *function_data)
{
    C_PCall d{function, function_data};
    lulu_Error e = vm_run_protected(vm, c_pcall, &d);
    return e;
}

LULU_API int
lulu_error(lulu_VM *vm)
{
    vm_throw(vm, LULU_ERROR_RUNTIME);
    return 0;
}

/*=== TYPE QUERY FUNCTIONS ========================================== {{{ */

LULU_API lulu_Type
lulu_type(lulu_VM *vm, int i)
{
    const Value *v = value_at(vm, i);
    if (v == &none) {
        return LULU_TYPE_NONE;
    }

    Value_Type t = v->type();
    lulu_assertf(VALUE_NIL <= t && t <= VALUE_TYPE_LAST, "Got Value_Type(%i)", t);
    return cast(lulu_Type)t;
}

LULU_API const char *
lulu_type_name(lulu_VM *vm, lulu_Type t)
{
    unused(vm);
    return (t == LULU_TYPE_NONE) ? "no value" : Value::type_names[t];
}

LULU_API int
lulu_is_number(lulu_VM *vm, int i)
{
    const Value *v = value_at(vm, i);
    Value tmp;
    return vm_to_number(v, &tmp);
}

LULU_API int
lulu_is_string(lulu_VM *vm, int i)
{
    // `number` is always convertible to a string.
    Value_Type t = value_at(vm, i)->type();
    return t == VALUE_NUMBER || t == VALUE_STRING;
}

/*=== }}} =============================================================== */

/*=== STACK MANIPULATION FUNCTIONS ================================== {{{ */

LULU_API int
lulu_to_boolean(lulu_VM *vm, int i)
{
    return !value_at(vm, i)->is_falsy();
}

LULU_API lulu_Number
lulu_to_number(lulu_VM *vm, int i)
{
    Value tmp;
    const Value *v = value_at(vm, i);
    if (vm_to_number(v, &tmp)) {
        return tmp.to_number();
    }
    return 0;
}

LULU_API lulu_Integer
lulu_to_integer(lulu_VM *vm, int i)
{
    return cast(lulu_Integer)lulu_to_number(vm, i);
}

LULU_API const char *
lulu_to_lstring(lulu_VM *vm, int i, size_t *n)
{
    const Value *v = value_at(vm, i);

    /**
     * @note(2025-08-02)
     *      This call is safe, because if `v == &api.cpp:none`, it has the
     *      `nil` tag and nothing is changed.
     */
    if (!vm_to_string(vm, const_cast<Value *>(v)))  {
        if (n != nullptr) {
            *n = 0;
        }
        return nullptr;
    }

    // Otherwise, conversion success.
    OString *s = v->to_ostring();
    if (n != nullptr) {
        *n = cast_usize(s->len);
    }
    return s->to_cstring();
}

LULU_API void *
lulu_to_pointer(lulu_VM *vm, int i)
{
    return value_at(vm, i)->to_pointer();
}

LULU_API int
lulu_get_top(lulu_VM *vm)
{
    return cast_int(len(vm->window));
}

LULU_API void
lulu_set_top(lulu_VM *vm, int i)
{
    if (i >= 0) {
        isize old_start = ptr_index(vm->stack, raw_data(vm->window));
        isize old_stop  = old_start + len(vm->window);
        isize new_stop  = old_start + cast_isize(i);
        if (new_stop > old_stop) {
            // If growing the window, initialize the new region to nil.
            auto extra = slice(vm->stack, old_stop, new_stop);
            fill(extra, nil);
        }
        vm->window = slice(vm->stack, old_start, new_stop);
    } else {
        lulu_assert(value_at_stack(vm, i));
        lulu_pop(vm, -i);
    }
}

LULU_API void
lulu_insert(lulu_VM *vm, int i)
{
    Value *start = value_at_stack(vm, i);

    // Copy by value as this stack slot is about to be replaced.
    Value v = *value_at(vm, -1);
    auto dst = slice_pointer(start + 1, end(vm->window));
    auto src = slice_pointer_len(start, len(dst));
    copy(dst, src);
    *start = v;
}

LULU_API void
lulu_remove(lulu_VM *vm, int i)
{
    Value *start = value_at_stack(vm, i);
    Value *stop  = value_at_stack(vm, -1);
    auto dst = slice_pointer_len(start, stop - start);
    auto src = slice_pointer_len(start + 1, len(dst));
    copy(dst, src);
    lulu_pop(vm, 1);
}

LULU_API void
lulu_pop(lulu_VM *vm, int n)
{
    isize i = len(vm->window) - cast_isize(n);
    vm->window = slice_until(vm->window, i);
}

LULU_API void
lulu_push_nil(lulu_VM *vm, int n)
{
    for (int i = 0; i < n; i++) {
        vm_push(vm, nil);
    }
}

LULU_API void
lulu_push_boolean(lulu_VM *vm, int b)
{
    vm_push(vm, Value::make_boolean(cast(bool)b));
}

LULU_API void
lulu_push_number(lulu_VM *vm, lulu_Number n)
{
    vm_push(vm, Value::make_number(n));
}

LULU_API void
lulu_push_integer(lulu_VM *vm, lulu_Integer i)
{
    vm_push(vm, Value::make_number(cast(Number)i));
}

LULU_API void
lulu_push_userdata(lulu_VM *vm, void *p)
{
    vm_push(vm, Value::make_userdata(p));
}

LULU_API void
lulu_push_lstring(lulu_VM *vm, const char *s, size_t n)
{
    LString ls{s, cast_isize(n)};
    OString *os = ostring_new(vm, ls);
    vm_push(vm, Value::make_string(os));
}

LULU_API void
lulu_push_string(lulu_VM *vm, const char *s)
{
    if (s == nullptr) {
        lulu_push_nil(vm, 1);
    } else {
        lulu_push_lstring(vm, s, strlen(s));
    }
}

LULU_API const char *
LULU_ATTR_PRINTF(2, 3)
lulu_push_fstring(lulu_VM *vm, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const char *s = vm_push_vfstring(vm, fmt, args);
    va_end(args);
    return s;
}

LULU_API const char *
lulu_push_vfstring(lulu_VM *vm, const char *fmt, va_list args)
{
    return vm_push_vfstring(vm, fmt, args);
}

LULU_API void
lulu_push_c_closure(lulu_VM *vm, lulu_C_Function cf, int n_upvalues)
{
    lulu_assert(n_upvalues >= 0);

    Closure *f = closure_c_new(vm, cf, n_upvalues);
    for (int i = 0; i < n_upvalues; i++) {
        Value v = *value_at_stack(vm, -n_upvalues + i);
        f->c.upvalues[i] = v;
    }
    lulu_pop(vm, n_upvalues);
    vm_push(vm, Value::make_function(f));
}

LULU_API void
lulu_push_value(lulu_VM *vm, int i)
{
    const Value *v = value_at(vm, i);
    vm_push(vm, *v);
}

/*=== }}} =============================================================== */

LULU_API void
lulu_new_table(lulu_VM *vm, int n_array, int n_hash)
{
    Table *t = table_new(vm, cast_isize(n_array), cast_isize(n_hash));
    vm_push(vm, Value::make_table(t));
}

LULU_API int
lulu_get_table(lulu_VM *vm, int table_index)
{
    const Value *t = value_at(vm, table_index);
    Value       *k = value_at_stack(vm, -1);
    if (t->is_table()) {
        // No need to push, `k` can be overwritten.
        bool ok = vm_table_get(vm, t, *k, k);
        return ok;
    }
    return false;
}

LULU_API int
lulu_get_field(lulu_VM *vm, int table_index, const char *key)
{
    const Value *t = value_at(vm, table_index);
    const Value  k = t->make_string(ostring_from_cstring(vm, key));

    // Unlike `lulu_get_table()`, we need to explicitly push `t[k]` because
    // `k` does not exist in the stack and thus cannot be replaced.
    if (t->is_table()) {
        Value v;
        bool  ok = table_get(t->to_table(), k, &v);
        vm_push(vm, v);
        return ok;
    }
    return false;
}

LULU_API void
lulu_set_table(lulu_VM *vm, int table_index)
{
    const Value *t = value_at(vm, table_index);
    if (t->is_table()) {
        const Value *k = value_at(vm, -2);
        const Value *v = value_at(vm, -1);
        vm_table_set(vm, t, k, *v);
        lulu_pop(vm, 2);
    }
}

LULU_API void
lulu_set_field(lulu_VM *vm, int table_index, const char *key)
{
    const Value *t = value_at(vm, table_index);
    const Value  k = Value::make_string(ostring_from_cstring(vm, key));
    if (t->is_table()) {
        // The value is popped implicitly. We have no way to tell if key is in
        // the stack.
        Value v = vm_pop(vm);
        vm_table_set(vm, t, &k, v);
    }
}

LULU_API int
lulu_next(lulu_VM *vm, int table_index)
{
    const Value *_t = value_at(vm, table_index);
    lulu_assert(_t->is_table());
    Table *t = _t->to_table();
    Value *k = value_at_stack(vm, -1);
    Value  v;
    bool more = table_next(vm, t, k, &v);
    if (more) {
        vm_push(vm, v);
    } else {
        // No more elements, remove the key.
        vm_pop(vm);
    }
    return cast_int(more);
}

LULU_API size_t
lulu_obj_len(lulu_VM *vm, int i)
{
    const Value *v = value_at(vm, i);
    if (v->is_string()) {
        return v->to_ostring()->len;
    }
    return 0;
}

LULU_API void
lulu_concat(lulu_VM *vm, int n)
{
    switch (n) {
    case 0: lulu_push_literal(vm, ""); return;
    case 1: return; // Nothing we can sensibly do, other than conversion.
    }

    lulu_assert(2 <= n && cast_isize(n) <= len(vm->window));

    Value *first = value_at_stack(vm, -n);
    Value *last  = value_at_stack(vm, -1);

    vm_concat(vm, first, slice_pointer(first, last + 1));

    // Pop all arguments except the first one- the one we replaced.
    lulu_pop(vm, n - 1);
}
