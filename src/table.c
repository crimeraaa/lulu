#include "table.h"
#include "string.h"
#include "memory.h"

#define TABLE_MAX_LOAD  0.75

static uint32_t hash_pointer(Object *ptr)
{
    char bytes[sizeof(ptr)];
    memcpy(bytes, &ptr, sizeof(bytes));
    return hash_rstring(make_strview(bytes, sizeof(bytes)));
}

static uint32_t hash_number(Number number)
{
    char bytes[sizeof(number)];
    memcpy(bytes, &number, sizeof(bytes));
    return hash_rstring(make_strview(bytes, sizeof(bytes)));
}

static void clear_entries(Entry *entries, int cap)
{
    for (int i = 0; i < cap; i++) {
        setv_nil(&entries[i].key);
        setv_nil(&entries[i].value);
    }
}

static uint32_t get_hash(const Value *self)
{
    switch (get_tag(self)) {
    case TYPE_NIL:      return 0; // WARNING: We should never hash `nil`!
    case TYPE_BOOLEAN:  return as_boolean(self);
    case TYPE_NUMBER:   return hash_number(as_number(self));
    case TYPE_STRING:   return as_string(self)->hash;
    case TYPE_TABLE:    return hash_pointer(as_object(self));
    }
}

// Find a free slot. Assumes there is at least 1 free slot left.
static Entry *find_entry(Entry *list, int cap, const Value *key)
{
    uint32_t index = get_hash(key) % cap;
    Entry   *tombstone = NULL;
    for (;;) {
        Entry *entry = &list[index];
        if (is_nil(&entry->key)) {
            if (is_nil(&entry->value)) {
                return (tombstone == NULL) ? entry : tombstone;
            } else if (tombstone == NULL) {
                tombstone = entry;
            }
        } else if (values_equal(&entry->key, key)) {
            return entry;
        }
        index = (index + 1) % cap;
    }
}

// Analogous to `adjustCapacity()` in the book. Assumes we only ever grow!
static void resize_table(Table *self, int newcap, Alloc *alloc)
{
    Entry *newbuf = new_array(Entry, newcap, alloc);
    clear_entries(newbuf, newcap);

    // Copy non-empty and non-tombstone entries to the new table.
    self->count = 0;
    for (int i = 0; i < self->cap; i++) {
        Entry *src = &self->entries[i];
        if (is_nil(&src->key)) {
            continue; // Throws away both empty and tombstone entries.
        }
        Entry *dst = find_entry(newbuf, newcap, &src->key);
        dst->key   = src->key;
        dst->value = src->value;
        self->count++;
    }
    free_array(Entry, self->entries, self->cap, alloc);
    self->entries = newbuf;
    self->cap     = newcap;
}

Table *new_table(Alloc *alloc, int size)
{
    Table *inst = cast(Table*, new_object(sizeof(*inst), TYPE_TABLE, alloc));
    init_table(inst);
    if (size > 0) {
        resize_table(inst, size, alloc);
    }
    return inst;
}

void init_table(Table *self)
{
    self->entries = NULL;
    self->count   = 0;
    self->cap     = 0;
}

void free_table(Table *self, Alloc *alloc)
{
    // free_array(Entry, self->entries, self->cap, alloc);
    free_parray(self->entries, self->cap, alloc);
    init_table(self);
}

void dump_table(const Table *self, const char *name)
{
    name = (name != NULL) ? name : "(anonymous table)";
    if (self->count == 0) {
        printf("%s = {}\n", name);
        return;
    }
    printf("%s = {\n", name);
    for (int i = 0, limit = self->cap; i < limit; i++) {
        const Entry *entry = &self->entries[i];
        if (is_nil(&entry->key)) {
            continue;
        }
        printf("\t[");
        print_value(&entry->key, true);
        printf("] = ");
        print_value(&entry->value, true);
        printf(",\n");
    }
    printf("}\n");
}

bool get_table(Table *self, const Value *key, Value *out)
{
    if (self->count == 0 || is_nil(key)) {
        return false;
    }
    Entry *entry = find_entry(self->entries, self->cap, key);
    if (is_nil(&entry->key)) {
        return false;
    }
    *out = entry->value;
    return true;
}

bool set_table(Table *self, const Value *key, const Value *val, Alloc *alloc)
{
    if (is_nil(key)) {
        return false;
    }
    if (self->count + 1 > self->cap * TABLE_MAX_LOAD) {
        resize_table(self, grow_capacity(self->cap), alloc);
    }
    Entry *entry    = find_entry(self->entries, self->cap, key);
    bool   isnewkey = is_nil(&entry->key);

    // Don't increase the count for tombstones (nil-key with non-nil value)
    if (isnewkey && is_nil(&entry->value)) {
        self->count++;
    }
    entry->key   = *key;
    entry->value = *val;
    return isnewkey;
}

bool unset_table(Table *self, const Value *key)
{
    if (self->count == 0 || is_nil(key)) {
        return false;
    }
    Entry *entry = find_entry(self->entries, self->cap, key);
    if (is_nil(&entry->key)) {
        return false;
    }
    // Place a tombstone, it must be distinct from a nil key with a nil value.
    setv_nil(&entry->key);
    setv_boolean(&entry->value, false);
    return true;
}

void copy_table(Table *dst, const Table *src, Alloc *alloc)
{
    for (int i = 0; i < src->cap; i++) {
        const Entry *entry = &src->entries[i];
        if (is_nil(&entry->key)) {
            continue;
        }
        set_table(dst, &entry->key, &entry->value, alloc);
    }
}
