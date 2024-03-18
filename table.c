#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

/* Tombstones use a nil key with a non-nil value to be distict. */
#define maketombstone       ((Entry){makenil, makeboolean(false)})
#define istombstone(e)      (isnil(&(e)->key) && isexactlyfalse(&(e)->value))
#define isempty(e)          (isnil(&(e)->key) && isnil(&(e)->value))

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

static DWord hash_number(lua_Number number) {
    union {
        lua_Number source;
        DWord bits[2];     // Use type punning to 'store' raw bits.
    } hash;
    hash.source = number;
    return byteunmask(hash.bits[1], 1) | hash.bits[0];
}

static DWord get_hash(const TValue *key) {
    switch (key->type) {
    case LUA_TBOOLEAN:  return (DWord)key->as.boolean; // Use indexes 0 and 1
    case LUA_TNUMBER:   return hash_number(asnumber(key));
    case LUA_TSTRING:   return asstring(key)->hash;
    case LUA_TFUNCTION: // Fall through
    case LUA_TTABLE:    return (uintptr_t)key->as.object;
    default:            return LUA_MAXDWORD; // Should never happen
    }
}

/**
 * III:20.4.2   Inserting Entries
 * 
 * Assumes that there is at least 1 free slot in the `entries` array. In the
 * function `table_set()` we do check that condition before calling this.
 */
static Entry *find_entry(Entry *entries, size_t cap, const TValue *key) {
    DWord index = get_hash(key) % cap;
    Entry *tombstone = NULL;
    for (;;) {
        Entry *entry = &entries[index];
        // nil might indicate a tombstone.
        if (isnil(&entry->key)) {
            if (isnil(&entry->value)) {
                // We prioritize returning tombstones.
                return tombstone != NULL ? tombstone : entry;
            } else {
                // Only set the found tombstone once.
                if (tombstone == NULL) {
                    tombstone = entry;
                }
            }
        } else if (values_equal(key, &entry->key)) {
            return entry;
        }
        index = (index + 1) % cap; // May wrap around back to start
    }
}

bool table_get(Table *self, const TValue *key, TValue *out) {
    // Empty tables cannot be (safely) indexed into, and in Lua, the value 'nil'
    // is not a valid key that the user can query.
    if (self->count == 0 || isnil(key)) {
        return false;
    }
    // Pointer to slot which may or may not have a value stored in it.
    Entry *entry = find_entry(self->entries, self->cap, key);
    
    // Nil keys can never refer to a valid index.
    if (isnil(&entry->key)) {
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
static void adjust_cap(Table *self, size_t cap) {
    // New array of new size.
    Entry *entries = allocate(Entry, cap);
    // 0-initialize each element
    for (size_t i = 0; i < cap; i++) {
        entries[i].key   = makenil;
        entries[i].value = makenil;
    }
    // Adjust entry placements as its always in relation to table size.
    // We reset the table count so we can omit tombstones.
    self->count = 0;
    for (size_t i = 0; i < self->cap; i++) {
        Entry *src = &self->entries[i];
        // If empty or a tombstone, skip it.
        if (isnil(&src->key)) {
            continue;
        }
        Entry *dst = find_entry(entries, cap, &src->key);
        dst->key = src->key;
        dst->value = src->value;
        self->count++;
    }
    deallocate_array(Entry, self->entries, self->cap);
    self->entries  = entries;
    self->cap = cap;
}

bool table_set(Table *self, const TValue *key, const TValue *value) {
    if (self->count + 1 > self->cap * TABLE_MAX_LOAD) {
        size_t cap = grow_cap(self->cap);
        adjust_cap(self, cap);
    }
    Entry *entry  = find_entry(self->entries, self->cap, key);
    bool isnewkey = isnil(&entry->key);
    if (isnewkey && isnil(&entry->value)) {
        // If we're replacing a tombstone with a new entry, the count's already 
        // been accounted for so we don't need to update it.
        self->count++;
    }
    entry->key   = *key; // Copy by value, whatever it may be.
    entry->value = *value;
    return isnewkey;
}

bool table_delete(Table *self, TValue *key) {
    if (self->cap == 0) {
        return false;
    }
    // As usual we find the entry to be deleted.
    Entry *entry = find_entry(self->entries, self->cap, key);
    if (isnilornone(&entry->key)) {
        return false;
    }
    // Place a tombstone in the entry. We use boolean `false` to distinguish it 
    // from nil. You can use any non-nil value you like, it just needs to be a
    // distinct value.
    *entry = maketombstone;
    return true;
}

void copy_table(Table *dst, Table *src) {
    for (size_t i = 0; i < src->cap; i++) {
        Entry *entry = &src->entries[i];
        // Only copy over non-empty and non-tombstone entries.
        if (!isnil(&entry->key)) {
            table_set(dst, &entry->key, &entry->value);
        }
    }
}

TString *table_findstring(Table *self, const char *data, size_t len, DWord hash) {
    if (self->count == 0) {
        return NULL;
    }
    DWord index = hash % self->cap;
    for (;;) {
        Entry *entry = &self->entries[index];
        if (isnil(&entry->key)) {
            // Stop if we find a non-tombstone entry, because tombstones are
            // indicated by having a non-nil value so we implicitly skip them.
            if (isnil(&entry->value)) {
                return NULL;
            }
        } else if (isstring(&entry->key)) {
            TString *s = asstring(&entry->key);
            if (s->len == len && s->hash == hash) {
                if (memcmp(s->data, data, len) == 0) {
                    return s;
                }
            }
        } 
        index = (index + 1) % self->cap;
    }
}

void print_table(const Table *self, bool dodump) {
    printf("table: %p", (void*)self);
    if (!dodump) {
        return;
    }
    printf("\n");
    if (self->count == 0) {
        return;
    }

    // self->count isn't necessarily correct, e.g. count of 5 yet we can have
    // valuesa at index 7 for example. So we prefer to use self->cap.
    for (size_t i = 0; i < self->cap; i++) {
        const Entry *entry = &self->entries[i];
        if (!isempty(entry)) {
            printf("\t[ K: ");
            print_value(&entry->key);
            printf(" ][ V: ");
            print_value(&entry->value);
            printf(" ]\n");
        }
    }
}
