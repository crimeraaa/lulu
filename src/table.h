#ifndef LULU_TABLE_H
#define LULU_TABLE_H

#include "string.h"

#define LULU_TABLE_MAX_LOAD     (0.75)

typedef struct {
    lulu_Value key;
    lulu_Value value;
} lulu_Table_Pair;

struct lulu_Table {
    lulu_Object      base;
    lulu_Table_Pair *pairs;
    isize count; // Number of active pairs.
    isize cap;   // Number of total allocated pairs.
};

lulu_Table *
lulu_Table_new(lulu_VM *vm);

void
lulu_Table_init(lulu_Table *self);

// If self is itself a heap-allocated pointer, we won't free it here.
void
lulu_Table_free(lulu_VM *vm, lulu_Table *self);

lulu_Value
lulu_Table_get(lulu_Table *self, const lulu_Value *key);

lulu_String *
lulu_Table_intern_string(lulu_VM *vm, lulu_Table *self, lulu_String *string);

lulu_String *
lulu_Table_find_string(lulu_Table *self, String string, u32 hash);

bool
lulu_Table_set(lulu_VM *vm, lulu_Table *self, const lulu_Value *key, const lulu_Value *value);

bool
lulu_Table_unset(lulu_Table *self, const lulu_Value *key);

#endif // LULU_TABLE_H
