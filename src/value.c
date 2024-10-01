#include "value.h"
#include "string.h"

#include <stdio.h>

// @todo 2024-09-22 Just remove the designated initializers entirely!
#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wc99-designator"
#endif

const cstring
LULU_TYPENAMES[LULU_TYPE_COUNT] = {
    [LULU_TYPE_NIL]     = "nil",
    [LULU_TYPE_BOOLEAN] = "boolean",
    [LULU_TYPE_NUMBER]  = "number",
    [LULU_TYPE_STRING]  = "string",
    [LULU_TYPE_TABLE]   = "table",
};

#if defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

/**
 * @note 2024-09-13
 *      Requires boolean as first member of the union in order for this to work.
 *      Otherwise, you'll need to rely on designated initializers.
 */
const lulu_Value LULU_VALUE_NIL   = {LULU_TYPE_NIL,     {0}};
const lulu_Value LULU_VALUE_TRUE  = {LULU_TYPE_BOOLEAN, {true}};
const lulu_Value LULU_VALUE_FALSE = {LULU_TYPE_BOOLEAN, {false}};

void
lulu_Value_print(const lulu_Value *value)
{
    switch (value->type) {
    case LULU_TYPE_NIL:
        printf("nil");
        break;
    case LULU_TYPE_BOOLEAN:
        printf("%s", value->boolean ? "true" : "false");
        break;
    case LULU_TYPE_NUMBER:
        printf(LULU_NUMBER_FMT, value->number);
        break;
    case LULU_TYPE_STRING:
        printf("%s", value->string->data);
        break;
    case LULU_TYPE_TABLE:
        printf("%s: %p", lulu_Value_typename(value), cast(void *)value->table);
        break;
    }
}
bool
lulu_Value_eq(const lulu_Value *a, const lulu_Value *b)
{
    if (a->type != b->type) {
        return false;
    }

    switch (a->type) {
    case LULU_TYPE_NIL:     return true;
    case LULU_TYPE_BOOLEAN: return a->boolean == b->boolean;
    case LULU_TYPE_NUMBER:  return lulu_Number_eq(a->number, b->number);
    case LULU_TYPE_STRING:  return a->string == b->string;
    case LULU_TYPE_TABLE:   return a->table == b->table;
    }
    return false;
}

void
lulu_Value_Array_init(lulu_Value_Array *self)
{
    self->values = NULL;
    self->len    = 0;
    self->cap    = 0;
}

void
lulu_Value_Array_write(lulu_VM *vm, lulu_Value_Array *self, const lulu_Value *value)
{
    if (self->len >= self->cap) {
        lulu_Value_Array_reserve(vm, self, lulu_Memory_grow_capacity(self->cap));
    }
    self->values[self->len++] = *value;
}

void
lulu_Value_Array_reserve(lulu_VM *vm, lulu_Value_Array *self, isize new_cap)
{
    isize old_cap = self->cap;
    // Nothing to do?
    if (new_cap <= old_cap) {
        return;
    }

    self->values = rawarray_resize(lulu_Value, vm, self->values, old_cap, new_cap);
    self->cap    = new_cap;
}

void
lulu_Value_Array_free(lulu_VM *vm, lulu_Value_Array *self)
{
    rawarray_free(lulu_Value, vm, self->values, self->cap);
    lulu_Value_Array_init(self);
}
