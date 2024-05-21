#ifndef LULU_TABLE_INTERNALS_H
#define LULU_TABLE_INTERNALS_H

#include "object.h"

// Used for user-created tables, not VM's globals/strings tables.
Table *new_table(Alloc *alloc, int size);
void init_table(Table *self);
void free_table(Table *self, Alloc *alloc);
void dump_table(const Table *self, const char *name);
bool get_table(Table *self, const Value *key, Value *out);
bool set_table(Table *self, const Value *key, const Value *value, Alloc *alloc);

// Place a tombstone value. Analogous to `deleteTable()` in the book.
bool unset_table(Table *self, const Value *key);

// Analogous to `tableAddAll()` in the book.
void copy_table(Table *dst, const Table *src, Alloc *alloc);

#endif /* LULU_TABLE_H */
