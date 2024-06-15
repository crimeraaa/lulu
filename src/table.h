#ifndef LULU_TABLE_INTERNALS_H
#define LULU_TABLE_INTERNALS_H

#include "object.h"

// Used for user-created tables, not VM's globals/strings tables.
Table *luluTbl_new(lulu_VM *vm, int size);
void luluTbl_init(Table *t);
void luluTbl_free(lulu_VM *vm, Table *t);
bool luluTbl_get(Table *t, const Value *k, Value *out);
bool luluTbl_set(lulu_VM *vm, Table *t, const Value *k, const Value *v);

// Place a tombstone value. Analogous to `deleteTable()` in the book.
bool luluTbl_unset(Table *t, const Value *k);

// Analogous to `tableAddAll()` in the book.
void luluTbl_copy(lulu_VM *vm, Table *dst, const Table *src);

#endif /* LULU_TABLE_H */
