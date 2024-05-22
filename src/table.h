#ifndef LULU_TABLE_INTERNALS_H
#define LULU_TABLE_INTERNALS_H

#include "object.h"

// Used for user-created tables, not VM's globals/strings tables.
Table *new_table(int size, struct lulu_Alloc *al);
void init_table(Table *t);
void free_table(Table *t, struct lulu_Alloc *al);
void dump_table(const Table *t, const char *name);
bool get_table(Table *t, const Value *k, Value *out);
bool set_table(Table *t, const Value *k, const Value *v, struct lulu_Alloc *al);

// Place a tombstone value. Analogous to `deleteTable()` in the book.
bool unset_table(Table *t, const Value *k);

// Analogous to `tableAddAll()` in the book.
void copy_table(Table *dst, const Table *src, struct lulu_Alloc *al);

#endif /* LULU_TABLE_H */
