#ifndef LUA_TABLE_H
#define LUA_TABLE_H

#include "common.h"
#include "value.h"

/**
 * III:20.4     Building a Hash Table
 * 
 * This is a key-value pair element to be thrown into the hash table. It contains
 * a "key" which is a currently a string that gets hashed, which determines the
 * actual numerical index in the table.
 */
typedef struct {
    lua_String *key; // Key to be hashed.
    TValue value;    // Actual value to be retrieved.
} Entry;

/**
 * III:20.4     Building a Hash Table
 * 
 * A hash table is a dynamic array with the unique property of contaning
 * "key-value" pair elements, of which the index is determined by a so-called
 * "hash function". It's useful for storing strings, but Lua extends them to
 * also store numbers, booleans, functions, tables, userdata and threads.
 * For now we'll only focus on hashing strings.
 */
typedef struct {
    Entry *entries; // List of hashed key-value pairs.
    int count;      // Current number of entries occupying the table.
    int capacity;   // How many entries the table can hold.
} Table;

void init_table(Table *self);
void free_table(Table *self);

/**
 * III:20.4.4   Retrieving Values
 * 
 * Unlike my initial intuition we use an out parameter. I'm not sure how much I
 * like it but I'll follow along for now.
 */
bool table_get(Table *self, const lua_String *key, TValue *out);

/**
 * III:20.4.2   Inserting entries
 * 
 * All our string objects know their hash code so we can already put them into
 * the hash table.
 */
bool table_set(Table *self, lua_String *key, TValue value);

/**
 * III:20.4.5   Deleting entries
 * 
 * Deleting an entry from hashtable that uses open addressing is a bit tricky,
 * because if the deleted element is in between collisions, how do you resolve
 * the retrieval of the ones that come after this one?
 */
bool table_delete(Table *self, lua_String *key);

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
 * table. Note that you pash a `const char*`, NOT a `lua_String*`! 
 * The point is to use this so we don't allocate a new `lua_String*`.
 */
lua_String *table_findstring(Table *self, const char *what, int length, DWord hash);

#endif /* LUA_TABLE_H */
