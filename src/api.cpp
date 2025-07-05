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
    size_t ii = (i > 0) ? cast_size(i) - 1 : len(vm->window) - cast_size(-i);
    return (ii < len(vm->window)) ? vm->window[ii] : VALUE_NONE_;
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
    lulu_Error e = vm_load(vm, LString(source), LString(script, script_size));
    return e;
}

void
lulu_call(lulu_VM *vm, int n_args, int n_rets)
{
    Value &fn = value_at(vm, -(n_args + 1));
    vm_call(vm, fn, n_args, (n_rets == LULU_MULTRET) ? VARARG : n_rets);
}

struct PCall_Data {
    int n_args, n_rets;
};

static void
pcall(lulu_VM *vm, void *user_ptr)
{
    PCall_Data *d = cast(PCall_Data *)user_ptr;
    lulu_call(vm, d->n_args, d->n_rets);
}

lulu_Error
lulu_pcall(lulu_VM *vm, int n_args, int n_rets)
{
    PCall_Data d{n_args, n_rets};
    lulu_Error e = vm_run_protected(vm, pcall, &d);
    return e;
}

struct C_PCall_Data {
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

LULU_API lulu_Error
lulu_c_pcall(lulu_VM *vm, lulu_CFunction function, void *function_data)
{
    C_PCall_Data d{function, function_data};
    lulu_Error e = vm_run_protected(vm, c_pcall, &d);
    return e;
}

LULU_API int
lulu_error(lulu_VM *vm)
{
    vm_throw(vm, LULU_ERROR_RUNTIME);
    return 0;
}

LULU_API void
lulu_register(lulu_VM *vm, const lulu_Register *library, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        OString *s = ostring_new(vm, LString(library[i].name));
        Closure *f = closure_new(vm, library[i].function);
        // TODO(2025-07-01): Ensure key and value are not collected!
        table_set(vm, &vm->globals, Value(s), Value(f));
    }
}

/*=== TYPE QUERY FUNCTIONS ============================================== {{{ */

lulu_Type
lulu_type(lulu_VM *vm, int i)
{
    Value_Type t = value_type(value_at(vm, i));
    lulu_assertf(VALUE_NONE <= t && t <= VALUE_FUNCTION, "Got Value_Type(%i)", t);
    return cast(lulu_Type)t;
}

const char *
lulu_type_name(lulu_VM *vm, int i)
{
    return value_type_name(cast(Value_Type)lulu_type(vm, i));
}

int
lulu_is_none(lulu_VM *vm, int i)
{
    return value_is_none(value_at(vm, i));
}

int
lulu_is_nil(lulu_VM *vm, int i)
{
    return value_is_nil(value_at(vm, i));
}

int
lulu_is_boolean(lulu_VM *vm, int i)
{
    return value_is_boolean(value_at(vm, i));
}

int
lulu_is_number(lulu_VM *vm, int i)
{
    return value_is_number(value_at(vm, i));
}

int
lulu_is_userdata(lulu_VM *vm, int i)
{
    return value_is_userdata(value_at(vm, i));
}

int
lulu_is_string(lulu_VM *vm, int i)
{
    return value_is_string(value_at(vm, i));
}

int
lulu_is_table(lulu_VM *vm, int i)
{
    return value_is_table(value_at(vm, i));
}

int
lulu_is_function(lulu_VM *vm, int i)
{
    return value_is_function(value_at(vm, i));
}

/*=== }}} =================================================================== */

/*=== STACK MANIPULATION FUNCTIONS ====================================== {{{ */

int
lulu_to_boolean(lulu_VM *vm, int i)
{
    Value v = value_at(vm, i);
    return !value_is_falsy(v);
}

lulu_Number
lulu_to_number(lulu_VM *vm, int i)
{
    Value v = value_at(vm, i);
    if (value_is_number(v)) {
        return value_to_number(v);
    }
    return 0;
}

const char *
lulu_to_lstring(lulu_VM *vm, int i, size_t *n)
{
    Value v = value_at(vm, i);
    if (value_is_string(v)) {
        OString *s = value_to_ostring(v);
        if (n != nullptr) {
            *n = s->len;
        }
        return s->data;
    }
    return nullptr;
}

void *
lulu_to_pointer(lulu_VM *vm, int i)
{
    Value v = value_at(vm, i);
    switch (v.type) {
    case VALUE_TABLE:       return value_to_table(v);
    case VALUE_FUNCTION:    return value_to_function(v);
    case VALUE_USERDATA:    return value_to_userdata(v);
    default:
        break;
    }
    return nullptr;
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
        size_t old_start = ptr_index(vm->stack, raw_data(vm->window));
        size_t old_stop  = old_start + len(vm->window);
        size_t new_stop  = old_start + cast_size(i);
        if (new_stop > old_stop) {
            // If growing the window, initialize the new region to nil.
            Slice<Value> extra{vm->stack, old_stop, new_stop};
            fill(extra, Value());
        }
        vm->window = Slice(vm->stack, old_start, new_stop);
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
    Slice<Value> dst{start + 1, end(vm->window)};
    Slice<Value> src{start,     len(dst)};
    copy(dst, src);
    *start = v;
}

void
lulu_remove(lulu_VM *vm, int i)
{
    Value *start = &value_at(vm, i);
    Value *stop  = &value_at(vm, -1);
    Slice<Value> dst{start,     stop - 1};
    Slice<Value> src{start + 1, len(dst)};
    copy(dst, src);
    lulu_pop(vm, 1);
}

void
lulu_pop(lulu_VM *vm, int n)
{
    size_t i = len(vm->window) - cast_size(n);
    vm->window = Slice(vm->window, 0, i);
}

void
lulu_push_nil(lulu_VM *vm, int n)
{
    for (int i = 0; i < n; i++) {
        vm_push(vm, Value());
    }
}

void
lulu_push_boolean(lulu_VM *vm, int b)
{
    vm_push(vm, Value(bool(b)));
}

void
lulu_push_number(lulu_VM *vm, lulu_Number n)
{
    vm_push(vm, Value(n));
}

void
lulu_push_userdata(lulu_VM *vm, void *p)
{
    vm_push(vm, Value(p));
}

void
lulu_push_lstring(lulu_VM *vm, const char *s, size_t n)
{
    OString *o = ostring_new(vm, LString(s, n));
    vm_push(vm, Value(o));
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
    vm_push(vm, Value(f));
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
    OString *o = ostring_new(vm, LString(s));
    Value k = Value(o);

    Value v;
    bool  ok = table_get(&vm->globals, k, &v);
    vm_push(vm, v);
    return cast_int(ok);
}

void
lulu_set_global(lulu_VM *vm, const char *s)
{
    OString *o = ostring_new(vm, LString(s));
    Value    v = vm_pop(vm);
    table_set(vm, &vm->globals, Value(o), v);
}

void
lulu_concat(lulu_VM *vm, int n)
{

    switch (n) {
    case 0: lulu_push_literal(vm, ""); return;
    case 1: return; // Nothing we can sensibly do, other than conversion.
    }

    lulu_assert(len(vm->window) >= cast_size(n));

    Value &first = value_at(vm, -n);
    Value &last  = value_at(vm, -1);

    // `value_at()` returned a sentinel value? We can't properly form a slice
    // with this.
    lulu_assert(!value_is_none(first));

    vm_concat(vm, first, Slice(&first, &last + 1));
    // Pop all arguments except the first one- the one we replaced.
    lulu_pop(vm, n - 1);
}
