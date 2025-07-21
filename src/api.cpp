#include "vm.hpp"

// Do not make `constexpr`; must have an address in order to be a reference.
static Value
none = nil;

static Value *
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
    return &none;
}

static Value *
value_at_stack(lulu_VM *vm, int i)
{
    Value *start = value_at(vm, i);
    lulu_assert(start != &none);
    lulu_assertf(i > LULU_PSEUDO_INDEX, "Got pseudo-index %i", i);
    return start;
}


lulu_VM *
lulu_open(lulu_Allocator allocator, void *allocator_data)
{
    static lulu_VM vm;
    bool ok = vm_init(&vm, allocator, allocator_data);
    if (!ok) {
        vm_destroy(&vm);
        return nullptr;
    }
    return &vm;
}

void
lulu_close(lulu_VM *vm)
{
    vm_destroy(vm);
}

lulu_Error
lulu_load(lulu_VM *vm, const char *source, const char *script, size_t script_size)
{
    LString lscript{script, cast_isize(script_size)};
    lulu_Error e = vm_load(vm, lstring_from_cstring(source), lscript);
    return e;
}

void
lulu_call(lulu_VM *vm, int n_args, int n_rets)
{
    Value *fn = value_at(vm, -(n_args + 1));
    vm_call(vm, fn, n_args, (n_rets == LULU_MULTRET) ? VARARG : n_rets);
}

struct LULU_PRIVATE PCall {
    int n_args, n_rets;
};

static void
pcall(lulu_VM *vm, void *user_ptr)
{
    PCall *d = cast(PCall *)user_ptr;
    Value *fn = value_at(vm, -(d->n_args + 1));
    vm_call(vm, fn, d->n_args, (d->n_rets == LULU_MULTRET) ? VARARG : d->n_rets);
}

lulu_Error
lulu_pcall(lulu_VM *vm, int n_args, int n_rets)
{
    PCall d{n_args, n_rets};
    lulu_Error e = vm_run_protected(vm, pcall, &d);
    return e;
}

struct LULU_PRIVATE C_PCall {
    lulu_CFunction function;
    void          *function_data;
};

static void
c_pcall(lulu_VM *vm, void *user_ptr)
{
    C_PCall *d = cast(C_PCall *)user_ptr;
    lulu_push_cfunction(vm, d->function);
    lulu_push_userdata(vm, d->function_data);
    lulu_call(vm, 1, 0);
}

lulu_Error
lulu_c_pcall(lulu_VM *vm, lulu_CFunction function, void *function_data)
{
    C_PCall d{function, function_data};
    lulu_Error e = vm_run_protected(vm, c_pcall, &d);
    return e;
}

int
lulu_error(lulu_VM *vm)
{
    vm_throw(vm, LULU_ERROR_RUNTIME);
    return 0;
}

/*=== TYPE QUERY FUNCTIONS ============================================== {{{ */

lulu_Type
lulu_type(lulu_VM *vm, int i)
{
    const Value *v = value_at(vm, i);
    if (v == &none) {
        return LULU_TYPE_NONE;
    }

    Value_Type t = v->type();
    lulu_assertf(VALUE_NIL <= t && t <= VALUE_USERDATA, "Got Value_Type(%i)", t);
    return cast(lulu_Type)t;
}

const char *
lulu_type_name(lulu_VM *vm, lulu_Type t)
{
    unused(vm);
    return (t == LULU_TYPE_NONE) ? "no value" : Value::type_names[t];
}

int
lulu_is_none(lulu_VM *vm, int i)
{
    const Value *v = value_at(vm, i);
    return (v == &none);
}

int
lulu_is_nil(lulu_VM *vm, int i)
{
    return value_at(vm, i)->is_nil();
}

int
lulu_is_boolean(lulu_VM *vm, int i)
{
    return value_at(vm, i)->is_boolean();
}

int
lulu_is_number(lulu_VM *vm, int i)
{
    const Value *v = value_at(vm, i);
    Value tmp;
    return vm_to_number(v, &tmp);
}

int
lulu_is_userdata(lulu_VM *vm, int i)
{
    return value_at(vm, i)->is_userdata();
}

int
lulu_is_string(lulu_VM *vm, int i)
{
    // `number` is always convertible to a string.
    Value_Type t = value_at(vm, i)->type();
    return t == VALUE_NUMBER || t == VALUE_STRING;
}

int
lulu_is_table(lulu_VM *vm, int i)
{
    return value_at(vm, i)->is_table();
}

int
lulu_is_function(lulu_VM *vm, int i)
{
    return value_at(vm, i)->is_function();
}

/*=== }}} =================================================================== */

/*=== STACK MANIPULATION FUNCTIONS ====================================== {{{ */

int
lulu_to_boolean(lulu_VM *vm, int i)
{
    return !value_at(vm, i)->is_falsy();
}

lulu_Number
lulu_to_number(lulu_VM *vm, int i)
{
    Value tmp;
    Value *v = value_at(vm, i);
    if (v->is_number()) {
        return v->to_number();
    } else if (vm_to_number(v, &tmp)) {
        return tmp.to_number();
    }
    return 0;
}

const char *
lulu_to_lstring(lulu_VM *vm, int i, size_t *n)
{
    Value *v = value_at(vm, i);
    if (!v->is_string()) {
        bool ok = vm_to_string(vm, v);
        if (!ok)  {
            if (n != nullptr) {
                *n = 0;
            }
            return nullptr;
        }
        // Otherwise, conversion success.
    }
    OString *s = v->to_ostring();
    if (n != nullptr) {
        *n = cast_usize(s->len);
    }
    return s->to_cstring();
}

void *
lulu_to_pointer(lulu_VM *vm, int i)
{
    return value_at(vm, i)->to_pointer();
}

int
lulu_get_top(lulu_VM *vm)
{
    return cast_int(len(vm->window));
}

void
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

void
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

void
lulu_remove(lulu_VM *vm, int i)
{
    Value *start = value_at_stack(vm, i);
    Value *stop  = value_at(vm, -1);
    auto dst = slice_pointer(start, stop - 1);
    auto src = slice_pointer_len(start + 1, len(dst));
    copy(dst, src);
    lulu_pop(vm, 1);
}

void
lulu_pop(lulu_VM *vm, int n)
{
    isize i = len(vm->window) - cast_isize(n);
    vm->window = slice_until(vm->window, i);
}

void
lulu_push_nil(lulu_VM *vm, int n)
{
    for (int i = 0; i < n; i++) {
        vm_push(vm, nil);
    }
}

void
lulu_push_boolean(lulu_VM *vm, int b)
{
    vm_push(vm, Value::make_boolean(cast(bool)b));
}

void
lulu_push_number(lulu_VM *vm, lulu_Number n)
{
    vm_push(vm, Value::make_number(n));
}

void
lulu_push_userdata(lulu_VM *vm, void *p)
{
    vm_push(vm, Value::make_userdata(p));
}

void
lulu_push_lstring(lulu_VM *vm, const char *s, size_t n)
{
    LString ls{s, cast_isize(n)};
    OString *os = ostring_new(vm, ls);
    vm_push(vm, Value::make_string(os));
}

void
lulu_push_string(lulu_VM *vm, const char *s)
{
    if (s == nullptr) {
        lulu_push_nil(vm, 1);
    } else {
        lulu_push_lstring(vm, s, strlen(s));
    }
}

const char *
lulu_push_fstring(lulu_VM *vm, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const char *s = vm_push_vfstring(vm, fmt, args);
    va_end(args);
    return s;
}

const char *
lulu_push_vfstring(lulu_VM *vm, const char *fmt, va_list args)
{
    return vm_push_vfstring(vm, fmt, args);
}

void
lulu_push_cfunction(lulu_VM *vm, lulu_CFunction cf)
{
    Closure *f = closure_new(vm, cf);
    vm_push(vm, Value::make_function(f));
}

void
lulu_push_value(lulu_VM *vm, int i)
{
    Value *v = value_at(vm, i);
    vm_push(vm, *v);
}

/*=== }}} =================================================================== */

void
lulu_new_table(lulu_VM *vm, int n_array, int n_hash)
{
    Table *t = table_new(vm, cast_isize(n_array), cast_isize(n_hash));
    vm_push(vm, Value::make_table(t));
}

int
lulu_get_table(lulu_VM *vm, int table_index)
{
    Value *t = value_at(vm, table_index);
    Value *k = value_at(vm, -1);
    if (t->is_table()) {
        // No need to push, `k` can be overwritten.
        bool ok = vm_table_get(vm, t, *k, k);
        return ok;
    }
    return false;
}

int
lulu_get_field(lulu_VM *vm, int table_index, const char *key)
{
    Value *t = value_at(vm, table_index);
    Value  k = t->make_string(ostring_from_cstring(vm, key));

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

void
lulu_set_table(lulu_VM *vm, int table_index)
{
    Value *t = value_at(vm, table_index);
    if (t->is_table()) {
        Value *k = value_at(vm, -2);
        Value *v = value_at(vm, -1);
        vm_table_set(vm, t, k, *v);
        lulu_pop(vm, 2);
    }
}

void
lulu_set_field(lulu_VM *vm, int table_index, const char *key)
{
    Value *t = value_at(vm, table_index);
    Value  k = Value::make_string(ostring_from_cstring(vm, key));
    if (t->is_table()) {
        // The value is popped implicitly. We have no way to tell if key is in
        // the stack.
        Value v = vm_pop(vm);
        vm_table_set(vm, t, &k, v);
    }
}

void
lulu_concat(lulu_VM *vm, int n)
{
    switch (n) {
    case 0: lulu_push_literal(vm, ""); return;
    case 1: return; // Nothing we can sensibly do, other than conversion.
    }

    lulu_assert(len(vm->window) >= cast_isize(n));

    Value *first = value_at(vm, -n);
    Value *last  = value_at(vm, -1);

    // `value_at()` returned a sentinel value? We can't properly form a slice
    // with this.
    lulu_assert(first != &none);

    vm_concat(vm, first, slice_pointer(first, last + 1));

    // Pop all arguments except the first one- the one we replaced.
    lulu_pop(vm, n - 1);
}
