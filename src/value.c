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

const Value
LULU_VALUE_NIL   = {.type = LULU_TYPE_NIL,     .number = 0},
LULU_VALUE_TRUE  = {.type = LULU_TYPE_BOOLEAN, .boolean = true},
LULU_VALUE_FALSE = {.type = LULU_TYPE_BOOLEAN, .boolean = false};

void
value_print(const Value *value)
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
        printf("%s: %p", value_typename(value), cast(void *)value->table);
        break;
    }
}
bool
value_eq(const Value *a, const Value *b)
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
varray_init(VArray *self)
{
    self->values = NULL;
    self->len    = 0;
    self->cap    = 0;
}

void
varray_write(lulu_VM *vm, VArray *self, const Value *value)
{
    if (self->len >= self->cap) {
        varray_reserve(vm, self, mem_grow_capacity(self->cap));
    }
    self->values[self->len++] = *value;
}

void
varray_reserve(lulu_VM *vm, VArray *self, isize new_cap)
{
    isize old_cap = self->cap;
    // Nothing to do?
    if (new_cap <= old_cap) {
        return;
    }

    self->values = array_resize(Value, vm, self->values, old_cap, new_cap);
    self->cap    = new_cap;
}

void
varray_free(lulu_VM *vm, VArray *self)
{
    array_free(Value, vm, self->values, self->cap);
    varray_init(self);
}
