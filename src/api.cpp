#include "vm.hpp"

// Do not make `constexpr`; must have an address in order to be a reference.
static Value
VALUE_NONE_{VALUE_NONE};

static constexpr Value &
value_at(lulu_VM *vm, int i)
{
    // Not valid in any way
    lulu_assert(i != 0);

    // Resolve 1-based relative index
    isize ii = (i > 0) ? cast_isize(i) - 1 : len(vm->window) - cast_isize(-i);
    return (0 <= ii && ii < len(vm->window)) ? vm->window[ii] : VALUE_NONE_;
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
    Value &fn = value_at(vm, -(n_args + 1));
    vm_call(vm, fn, n_args, (n_rets == LULU_MULTRET) ? VARARG : n_rets);
}

struct LULU_PRIVATE PCall_Data {
    int n_args, n_rets;
};

static void
pcall(lulu_VM *vm, void *user_ptr)
{
    PCall_Data *d = cast(PCall_Data *)user_ptr;
    Value &fn = value_at(vm, -(d->n_args + 1));
    vm_call(vm, fn, d->n_args, (d->n_rets == LULU_MULTRET) ? VARARG : d->n_rets);
}

lulu_Error
lulu_pcall(lulu_VM *vm, int n_args, int n_rets)
{
    PCall_Data d{n_args, n_rets};
    lulu_Error e = vm_run_protected(vm, pcall, &d);
    return e;
}

struct LULU_PRIVATE C_PCall_Data {
    lulu_CFunction function;
    void          *function_data;
};

static void
c_pcall(lulu_VM *vm, void *user_ptr)
{
    C_PCall_Data *d = cast(C_PCall_Data *)user_ptr;
    lulu_push_cfunction(vm, d->function);
    lulu_push_userdata(vm, d->function_data);
    lulu_call(vm, 1, 0);
}

lulu_Error
lulu_c_pcall(lulu_VM *vm, lulu_CFunction function, void *function_data)
{
    C_PCall_Data d{function, function_data};
    lulu_Error e = vm_run_protected(vm, c_pcall, &d);
    return e;
}

int
lulu_error(lulu_VM *vm)
{
    vm_throw(vm, LULU_ERROR_RUNTIME);
    return 0;
}

void
lulu_register(lulu_VM *vm, const lulu_Register *library, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        OString *s = ostring_new(vm, lstring_from_cstring(library[i].name));
        Closure *f = closure_new(vm, library[i].function);
        // TODO(2025-07-01): Ensure key and value are not collected!
        table_set(vm, &vm->globals, Value::make_string(s), Value::make_function(f));
    }
}

/*=== TYPE QUERY FUNCTIONS ============================================== {{{ */

lulu_Type
lulu_type(lulu_VM *vm, int i)
{
    Value_Type t = value_at(vm, i).type();
    lulu_assertf(VALUE_NONE <= t && t <= VALUE_FUNCTION, "Got Value_Type(%i)", t);
    return cast(lulu_Type)t;
}

const char *
lulu_type_name(lulu_VM *vm, lulu_Type t)
{
    unused(vm);
    return Value::type_name(cast(Value_Type)t);
}

int
lulu_is_none(lulu_VM *vm, int i)
{
    return value_at(vm, i).is_none();
}

int
lulu_is_nil(lulu_VM *vm, int i)
{
    return value_at(vm, i).is_nil();
}

int
lulu_is_boolean(lulu_VM *vm, int i)
{
    return value_at(vm, i).is_boolean();
}

int
lulu_is_number(lulu_VM *vm, int i)
{
    return value_at(vm, i).is_number();
}

int
lulu_is_userdata(lulu_VM *vm, int i)
{
    return value_at(vm, i).is_userdata();
}

int
lulu_is_string(lulu_VM *vm, int i)
{
    return value_at(vm, i).is_string();
}

int
lulu_is_table(lulu_VM *vm, int i)
{
    return value_at(vm, i).is_table();
}

int
lulu_is_function(lulu_VM *vm, int i)
{
    return value_at(vm, i).is_function();
}

/*=== }}} =================================================================== */

/*=== STACK MANIPULATION FUNCTIONS ====================================== {{{ */

int
lulu_to_boolean(lulu_VM *vm, int i)
{
    return !value_at(vm, i).is_falsy();
}

lulu_Number
lulu_to_number(lulu_VM *vm, int i)
{
    Value v = value_at(vm, i);
    if (v.is_number()) {
        return v.to_number();
    }
    return 0;
}

const char *
lulu_to_lstring(lulu_VM *vm, int i, size_t *n)
{
    Value v = value_at(vm, i);
    if (v.is_string()) {
        OString *s = v.to_ostring();
        if (n != nullptr) {
            *n = cast_usize(s->len);
        }
        return s->to_cstring();
    }
    return nullptr;
}

void *
lulu_to_pointer(lulu_VM *vm, int i)
{
    return value_at(vm, i).to_pointer();
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
        lulu_pop(vm, -i);
    }
}

void
lulu_insert(lulu_VM *vm, int i)
{
    Value *start = &value_at(vm, i);
    // Copy by value as this stack slot is about to be replaced.
    Value v = value_at(vm, -1);
    auto dst = slice_pointer(start + 1, end(vm->window));
    auto src = slice_from(dst, ptr_index(dst, start));
    copy(dst, src);
    *start = v;
}

void
lulu_remove(lulu_VM *vm, int i)
{
    Value *start = &value_at(vm, i);
    Value *stop  = &value_at(vm, -1);
    auto dst = slice_pointer(start, stop - 1);
    auto src = slice_from(dst, ptr_index(dst, start) + 1);
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
    vm_push(vm, cast(bool)b);
}

void
lulu_push_number(lulu_VM *vm, lulu_Number n)
{
    vm_push(vm, n);
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
    OString *o = ostring_new(vm, ls);
    vm_push(vm, Value::make_string(o));
}

void
lulu_push_string(lulu_VM *vm, const char *s)
{
    return lulu_push_lstring(vm, s, strlen(s));
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
    Value v = value_at(vm, i);
    vm_push(vm, v);
}

/*=== }}} =================================================================== */


int
lulu_get_global(lulu_VM *vm, const char *s)
{
    OString *o = ostring_new(vm, lstring_from_cstring(s));
    Value k = Value::make_string(o);

    Value v;
    bool  ok = table_get(&vm->globals, k, &v);
    vm_push(vm, v);
    return cast_int(ok);
}

void
lulu_set_global(lulu_VM *vm, const char *s)
{
    OString *o = ostring_new(vm, lstring_from_cstring(s));
    Value    v = vm_pop(vm);
    table_set(vm, &vm->globals, Value::make_string(o), v);
}

void
lulu_concat(lulu_VM *vm, int n)
{

    switch (n) {
    case 0: lulu_push_literal(vm, ""); return;
    case 1: return; // Nothing we can sensibly do, other than conversion.
    }

    lulu_assert(len(vm->window) >= cast_isize(n));

    Value &first = value_at(vm, -n);
    Value &last  = value_at(vm, -1);

    // `value_at()` returned a sentinel value? We can't properly form a slice
    // with this.
    lulu_assert(!first.is_none());

    vm_concat(vm, first, slice_pointer(&first, &last + 1));
    // Pop all arguments except the first one- the one we replaced.
    lulu_pop(vm, n - 1);
}
