/// local
#include "debug.h"
#include "vm.h"

/// standard
#include <string.h> // strlen

/**
 * @brief
 *      Given the relative index 'offset' (positive or negative), load the
 *      appropriate 'Value' pointer from either the top or the bottom
 *      of the stack.
 *
 * @link
 *      https://www.lua.org/source/5.1/lapi.c.html#index2adr
 */
static Value *
offset_to_address(lulu_VM *vm, int offset)
{
    isize start     = (offset >= 0) ? 0 : (vm->top - vm->base);
    isize abs_index = start + offset;
    isize end_index = vm->end - vm->base;

    // Abs. index is in range?
    if (0 <= abs_index && abs_index < end_index) {
        return &vm->values[abs_index];
    }
    vm_runtime_error(vm, "Out of bounds stack index %ti", abs_index);
    return NULL;
}

static void
push_safe(lulu_VM *vm, const Value *value)
{
    lulu_check_stack(vm, 1);
    vm->top[0] = *value;
    vm->top++;
}

void
lulu_check_stack(lulu_VM *vm, int count)
{
    // Loaded potentially invalid pointers is not standard, so this will have
    // to do instead.
    isize cur_index = vm->top - vm->base;
    isize end_index = vm->end - vm->base;
    isize new_index = cur_index + count;

    if (0 <= new_index && new_index < end_index) {
        return;
    }
    vm_runtime_error(vm, "Stack %s", (new_index >= 0) ? "overflow" : "underflow");
}

///=== TYPE QUERY FUNCTIONS ====================================================

lulu_Value_Type
lulu_type(lulu_VM *vm, int offset)
{
    return offset_to_address(vm, offset)->type;
}

cstring
lulu_typename(lulu_VM *vm, int offset)
{
    return value_typename(offset_to_address(vm, offset));
}

bool
lulu_is_nil(lulu_VM *vm, int offset)
{
    return value_is_nil(offset_to_address(vm, offset));
}

bool
lulu_is_boolean(lulu_VM *vm, int offset)
{
    return value_is_boolean(offset_to_address(vm, offset));
}

bool
lulu_is_number(lulu_VM *vm, int offset)
{
    return value_is_number(offset_to_address(vm, offset));
}

bool
lulu_is_string(lulu_VM *vm, int offset)
{
    return value_is_string(offset_to_address(vm, offset));
}

bool
lulu_is_table(lulu_VM *vm, int offset)
{
    return value_is_table(offset_to_address(vm, offset));
}

///=============================================================================

///=== STACK MANIPULATION FUNCTIONS ============================================

void
lulu_pop(lulu_VM *vm, int count)
{
    vm->top -= count;
}

void
lulu_push_nil(lulu_VM *vm, int count)
{
    lulu_check_stack(vm, count);
    Value *stack = vm->top;
    for (int i = 0; i < count; i++) {
        value_set_nil(&stack[i]);
    }
    vm->top += count;
}

void
lulu_push_boolean(lulu_VM *vm, bool boolean)
{
    push_safe(vm, (boolean) ? &LULU_VALUE_TRUE : &LULU_VALUE_FALSE);
}

void
lulu_push_number(lulu_VM *vm, lulu_Number number)
{
    Value tmp;
    value_set_number(&tmp, number);
    push_safe(vm, &tmp);
}

void
lulu_push_cstring(lulu_VM *vm, cstring cstr)
{
    lulu_push_string(vm, cstr, cast(isize)strlen(cstr));
}

void
lulu_push_string(lulu_VM *vm, const char *data, isize len)
{
    Value tmp;
    value_set_string(&tmp, ostring_new(vm, data, len));
    push_safe(vm, &tmp);
}

void
lulu_push_table(lulu_VM *vm, int n_hash, int n_array)
{
    Value tmp;
    value_set_table(&tmp, table_new(vm, n_hash, n_array));
    push_safe(vm, &tmp);
}

void
lulu_push_value(lulu_VM *vm, int stack_index)
{
    push_safe(vm, offset_to_address(vm, stack_index));
}

///=============================================================================
