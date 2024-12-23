/// local
#include "debug.h"
#include "vm.h"

/// standard
#include <string.h>

/**
 * @brief
 *      Given the relative index 'offset' (positive or negative), load the
 *      appropriate 'lulu_Value' pointer from either the top or the bottom
 *      of the stack.
 *
 * @warning 2024-09-29
 *      May load a potentially invalid address!
 *
 * @link
 *      https://www.lua.org/source/5.1/lapi.c.html#index2adr
 */
static lulu_Value *
offset_to_address(lulu_VM *vm, int offset)
{
    lulu_Value *start = (offset >= 0) ? vm->base : vm->top;
    return start + offset;
}

static void
push_safe(lulu_VM *vm, const lulu_Value *value)
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

    if (0 <= new_index && new_index <= end_index) {
        return;
    }
    lulu_VM_runtime_error(vm, "Stack %s", (new_index >= 0) ? "overflow" : "underflow");
}

///=== TYPE QUERY FUNCTIONS ====================================================

cstring
lulu_typename(lulu_VM *vm, int offset)
{
    return lulu_Value_typename(offset_to_address(vm, offset));
}

bool
lulu_is_nil(lulu_VM *vm, int offset)
{
    return lulu_Value_is_nil(offset_to_address(vm, offset));
}

bool
lulu_is_boolean(lulu_VM *vm, int offset)
{
    return lulu_Value_is_boolean(offset_to_address(vm, offset));
}

bool
lulu_is_number(lulu_VM *vm, int offset)
{
    return lulu_Value_is_number(offset_to_address(vm, offset));
}

bool
lulu_is_string(lulu_VM *vm, int offset)
{
    return lulu_Value_is_string(offset_to_address(vm, offset));
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
    for (int i = 0; i < count; i++) {
        vm->top[i] = LULU_VALUE_NIL;
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
    lulu_Value tmp;
    lulu_Value_set_number(&tmp, number);
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
    lulu_Value tmp;
    lulu_Value_set_string(&tmp, lulu_String_new(vm, data, len));
    push_safe(vm, &tmp);
}

void
lulu_push_table(lulu_VM *vm, isize count)
{
    lulu_Value tmp;
    lulu_Value_set_table(&tmp, lulu_Table_new(vm, count));
    push_safe(vm, &tmp);
}

///=============================================================================
