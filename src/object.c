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
    switch (self->tag) {
    case TYPE_NIL:
        printf("nil");
        break;
    case TYPE_BOOLEAN:
        printf(self->as.boolean ? "true" : "false");
        break;
    case TYPE_NUMBER:
        printf(LULU_NUMBER_FMT, self->as.number);
        break;
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
