#include "lulu.h"

#include "value.hpp"
#include "object.hpp"
#include "vm.hpp"
#include "table.hpp"

// Do not make `constexpr`; must have an address in order to be a reference.
static Value
VALUE_NONE_{VALUE_NONE};

static Value
value_at(lulu_VM &vm, int i)
{
    if (i > 0) {
        size_t ii = cast_size(i) - 1;
        if (ii < len(vm.window)) {
            return vm.window[ii];
        }
    } else {
        size_t ii = len(vm.window) - cast_size(-i);
        if (ii < len(vm.window)) {
            return vm.window[ii];
        }
    }
    return VALUE_NONE_;
}

lulu_VM *
lulu_open(lulu_Allocator allocator, void *allocator_data)
{
    static lulu_VM vm;
    vm_init(vm, allocator, allocator_data);
    return &vm;
}

void
lulu_close(lulu_VM *vm)
{
    vm_destroy(*vm);
}

lulu_Error
lulu_load(lulu_VM *vm, const char *source, const char *script, size_t script_size)
{
    lulu_Error e = vm_interpret(*vm, String(source), String(script, script_size));
    return e;
}

lulu_Type
lulu_type(lulu_VM *vm, int i)
{
    Value_Type t = value_type(value_at(*vm, i));
    lulu_assertf(VALUE_NIL <= t && t <= VALUE_FUNCTION, "Got Value_Type(%i)", t);
    return cast(lulu_Type)t;
}

const char *
lulu_type_name(lulu_VM *vm, int i)
{
    return value_type_name(cast(Value_Type)lulu_type(vm, i));
}

int
lulu_is_nil(lulu_VM *vm, int i)
{
    return value_is_nil(value_at(*vm, i));
}

int
lulu_is_boolean(lulu_VM *vm, int i)
{
    return value_is_boolean(value_at(*vm, i));
}

int
lulu_is_number(lulu_VM *vm, int i)
{
    return value_is_number(value_at(*vm, i));
}

int
lulu_is_string(lulu_VM *vm, int i)
{
    return value_is_string(value_at(*vm, i));
}

int
lulu_is_table(lulu_VM *vm, int i)
{
    return value_is_table(value_at(*vm, i));
}

int
lulu_is_function(lulu_VM *vm, int i)
{
    return value_is_function(value_at(*vm, i));
}


bool
lulu_to_boolean(lulu_VM *vm, int i)
{
    Value v = value_at(*vm, i);
    return !value_is_falsy(v);
}

lulu_Number
lulu_to_number(lulu_VM *vm, int i)
{
    Value v = value_at(*vm, i);
    if (value_is_number(v)) {
        return value_to_number(v);
    }
    return 0;
}

const char *
lulu_to_string(lulu_VM *vm, int i, size_t *n)
{
    Value v = value_at(*vm, i);
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
    Value v = value_at(*vm, i);
    if (value_is_object(v)) {
        return cast(void *)value_to_object(v);
    }
    return nullptr;
}


void
lulu_push_nil(lulu_VM *vm, int n)
{
    for (int i = 0; i < n; i++) {
        vm_push(*vm, Value());
    }
}

void
lulu_push_boolean(lulu_VM *vm, int b)
{
    vm_push(*vm, Value(bool(b)));
}

void
lulu_push_number(lulu_VM *vm, lulu_Number n)
{
    vm_push(*vm, Value(n));
}

void
lulu_push_string(lulu_VM *vm, const char *s, size_t n)
{
    OString *o = ostring_new(*vm, String(s, n));
    vm_push(*vm, Value(o));
}

void
lulu_push_cfunction(lulu_VM *vm, lulu_CFunction cf)
{
    Function *f = function_new(*vm, cf);
    vm_push(*vm, Value(f));
}

void
lulu_set_global(lulu_VM *vm, const char *s)
{
    OString *o = ostring_new(*vm, String(s));
    Value    v = vm_pop(*vm);
    table_set(*vm, vm->globals, Value(o), v);
}
