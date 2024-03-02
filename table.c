#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

/**
 * III:20.4.2   Inserting Entries
 * 
 * Even if the table isn't 100% full yet we try to grow it before it gets to that
 * point. So we arbitrarily choose to grow it when it's 75% full. If you'd like,
 * you can benchmark these things and change the load factor appropriately.
 */
#define TABLE_MAX_LOAD 0.75

void init_table(Table *self) {
    self->entries = NULL;
    self->count = 0;
    self->cap = 0;
}

void free_table(Table *self) {
    deallocate_array(Entry, self->entries, self->cap);
    init_table(self);
}

/**
 * III:20.4.2   Inserting Entries
 * 
 * Assumes that there is at least 1 free slot in the `entries` array. In the
 * function `table_set()` we do check that condition before calling this.
 */
static Entry *find_entry(Entry *entries, int cap, const lua_String *key) {
    DWord index = (key->hash % cap);
    Entry *tombstone = NULL;
    for (;;) {
        Entry *entry = &entries[index];
        if (entry->key == NULL) {
            if (isnil(entry->value)) {
                // We prioritize returning tombstones.
                return tombstone != NULL ? tombstone : entry;
            } else {
                // Only set the found tombstone once.
                if (tombstone == NULL) {
                    tombstone = entry;
                }
            }
        } else if (entry->key == key) {
            return entry;
        }
        index = (index + 1) % cap; // May wrap around back to start
    }
}

bool table_get(Table *self, const lua_String *key, TValue *out) {
    // Empty tables cannot be (safely) indexed into.
    if (self->count == 0) {
        return false;
    }
    // Pointer to slot which may or may not have a value stored in it.
    Entry *entry = find_entry(self->entries, self->cap, key);
    if (entry->key == NULL) {
        return false;
    }
    *out = entry->value;
    return true;
}

/**
 * III:20.4.3   Allocating and resizing
 * 
 * This is a bit of work since we can't just immediately allocate a new array,
 * free the old one and be done with it. Each entry is placed into the table in
 * regards to the table's size, so we need to do default initialization for each
 * element and then determine which of the original table's elements must be
 * copied over to the new one.
 */
static void adjust_cap(Table *self, int cap) {
    // New array of new size.
    Entry *entries = allocate(Entry, cap);
    // 0-initialize each element
    for (int i = 0; i < cap; i++) {
        entries[i].key = NULL;
        entries[i].value = makenil;
    }
    // Adjust entry placements as its always in relation to table size.
    // We reset the table count so we can omit tombstones.
    self->count = 0;
    for (int i = 0; i < self->cap; i++) {
        Entry *src = &self->entries[i];
        // If empty or a tombstone, skip it.
        if (src->key == NULL) {
            continue;
        }
        Entry *dst = find_entry(entries, cap, src->key);
        dst->key = src->key;
        dst->value = src->value;
        self->count++;
    }
    deallocate_array(Entry, self->entries, self->cap);
    self->entries  = entries;
    self->cap = cap;
}

bool table_set(Table *self, lua_String *key, TValue value) {
    if (self->count + 1 > self->cap * TABLE_MAX_LOAD) {
        int cap = grow_cap(self->cap);
        adjust_cap(self, cap);
    }
    Entry *entry = find_entry(self->entries, self->cap, key);
    bool isnewkey = (entry->key == NULL);
    // If we're replacing a tombstone with a new entry, the count's already been
    // accounted for so we don't need to update it.
    if (isnewkey && isnil(entry->value)) {
        self->count++;
    }
    entry->key = key;
    entry->value = value;
    return isnewkey;
}

bool table_delete(Table *self, lua_String *key) {
    if (self->cap == 0) {
        return false;
    }
    // As usual we find the entry to be deleted.
    Entry *entry = find_entry(self->entries, self->cap, key);
    if (entry->key == NULL) {
        return false;
    }
    // Place a tombstone in the entry. We use boolean `true` to distinguish it 
    // from nil. You can use any non-nil.
    entry->key = NULL;
    entry->value = makeboolean(false);
    return true;
}

void copy_table(Table *dst, Table *src) {
    for (int i = 0; i < src->cap; i++) {
        Entry *entry = &src->entries[i];
        if (entry->key != NULL) {
            table_set(dst, entry->key, entry->value);
        }
    }
}

lua_String *table_findstring(Table *self, const char *data, int len, DWord hash) {
    if (self->count == 0) {
        return NULL;
    }
    DWord index = hash % self->cap;
    for (;;) {
        Entry *entry = &self->entries[index];
        if (entry->key == NULL) {
            // Stop if we find a non-tombstone entry.
            if (isnil(entry->value)) {
                return NULL;
            }
        } else if (entry->key->len == len && entry->key->hash == hash) {
            if (memcmp(entry->key->data, data, len) == 0) {
                return entry->key; // We found it!
            }
        }
        index = (index + 1) % self->cap;
    }
}
