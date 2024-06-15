#ifndef LULU_TABLE_INTERNALS_H
#define LULU_TABLE_INTERNALS_H

#include "object.h"

// Used for user-created tables, not VM's globals/strings tables.
Table *new_table(lulu_VM *vm, int size);
void init_table(lulu_VM *vm, Table *t);
void free_table(lulu_VM *vm, Table *t);
bool get_table(lulu_VM *vm, Table *t, const Value *k, Value *out);
bool set_table(lulu_VM *vm, Table *t, const Value *k, const Value *v);

// Place a tombstone value. Analogous to `deleteTable()` in the book.
bool unset_table(lulu_VM *vm, Table *t, const Value *k);

// Analogous to `tableAddAll()` in the book.
void copy_table(lulu_VM *vm, Table *dst, const Table *src);

#endif /* LULU_TABLE_H */
