#ifndef LULU_TABLE_H
#define LULU_TABLE_H

#include "string.h"

#define LULU_TABLE_MAX_LOAD     0.75

typedef struct {
    Value key;
    Value value;
} Pair;

struct Table {
    Object  base;
    VArray  array;
    Pair   *pairs;
    int     n_pairs; // Number of active pairs.
    int     cap;   // Number of total allocated pairs.
};

Table *
table_new(lulu_VM *vm, int n_hash, int n_array);

void
table_init(Table *self);

// If self is itself a heap-allocated pointer, we won't free it here.
void
table_free(lulu_VM *vm, Table *self);

/**
 * @warning 2024-09-29
 *      If 'key' does not exist in the table, we return 'NULL'.
 *      This is to differentiate valid keys from invalid ones.
 */
const Value *
table_get(Table *self, const Value *key);

OString *
table_intern_string(lulu_VM *vm, Table *self, OString *string);

OString *
table_find_string(Table *self, const char *data, isize len, u32 hash);

bool
table_set(lulu_VM *vm, Table *self, const Value *key, const Value *value);

bool
table_set_hash(lulu_VM *vm, Table *self, const Value *key, const Value *value);

void
table_set_array(lulu_VM *vm, Table *table, VArray *array, int index, const Value *value);

bool
table_unset(Table *self, const Value *key);

#endif // LULU_TABLE_H
