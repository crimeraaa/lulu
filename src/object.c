#include "object.h"
#include "limits.h"
#include "memory.h"

const char *const LULU_TYPENAMES[] = {
    [TYPE_NIL]     = "nil",
    [TYPE_BOOLEAN] = "boolean",
    [TYPE_NUMBER]  = "number",
};

static_assert(arraylen(LULU_TYPENAMES) == NUM_TYPES, "Bad typename count");

void print_value(const TValue *self) {
    switch (get_tagtype(self)) {
    case TYPE_NIL:
        printf("nil");
        break;
    case TYPE_BOOLEAN:
        printf(as_boolean(self) ? "true" : "false");
        break;
    case TYPE_NUMBER:
        printf(NUMBER_FMT, as_number(self));
        break;
    }
}

bool values_equal(const TValue *lhs, const TValue *rhs) {
    // Logically, differing types can never be equal.
    if (get_tagtype(lhs) != get_tagtype(rhs)) {
        return false;
    }
    switch (get_tagtype(lhs)) {
    case TYPE_NIL:      return true;
    case TYPE_BOOLEAN:  return as_boolean(lhs) == as_boolean(rhs);
    case TYPE_NUMBER:   return num_eq(as_number(lhs), as_number(rhs));
    default:
        // Should not happen
        return false;
    }
}

void init_tarray(TArray *self) {
    self->values = NULL;
    self->len = 0;
    self->cap = 0;
}

void free_tarray(TArray *self) {
    free_array(TValue, self->values, self->len);
    init_tarray(self);
}

void write_tarray(TArray *self, const TValue *value) {
    if (self->len + 1 > self->cap) {
        int oldcap = self->cap;
        self->cap = grow_capacity(oldcap);
        self->values = grow_array(TValue, self->values, oldcap, self->cap);
    }
    self->values[self->len] = *value;
    self->len++;
}
