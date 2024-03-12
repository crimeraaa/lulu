#ifndef LUA_TABLE_H
#define LUA_TABLE_H

#include "common.h"
#include "object.h"
#include "value.h"

void init_table(Table *self);
void free_table(Table *self);

/**
 * III:20.4.4   Retrieving Values
 * 
 * Unlike my initial intuition we use an out parameter. I'm not sure how much I
 * like it but I'll follow along for now.
 */
bool table_get(Table *self, const TValue *key, TValue *out);

/**
 * III:20.4.2   Inserting entries
 * 
 * All our string objects know their hash code so we can already put them into
 * the hash table.
 */
bool table_set(Table *self, const TValue *key, const TValue *value);

/**
 * III:20.4.5   Deleting entries
 * 
 * Deleting an entry from hashtable that uses open addressing is a bit tricky,
 * because if the deleted element is in between collisions, how do you resolve
 * the retrieval of the ones that come after this one?
 */
bool table_delete(Table *self, TValue *key);

/**
 * III:20.4.3   Allocating and resizing
 * 
 * This is a helper function to copy all the entries from one table to another.
 * Do note that Rob's signature is `tableAddAll(Table *from, Table *to)` whereas
 * I prefer a `dst-src` signature.
 * 
 * We walk the array of `src` and only copy over non-empty elements.
 */
void copy_table(Table *dst, Table *src);

/**
 * III:20.5     String Interning
 *
 * This function checks if a given string has been interned in the given hash
 * table. Note that you pash a `const char*`, NOT a `TString*`! 
 * The point is to use this so we don't allocate a new `TString*`.
 */
TString *table_findstring(Table *self, const char *what, size_t len, DWord hash);

void print_table(const Table *self, bool dodump);

#endif /* LUA_TABLE_H */
