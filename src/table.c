#include "table.h"
#include "string.h"
#include "memory.h"

#define TABLE_MAX_LOAD  0.75

static uint32_t hash_pointer(Object *obj)
{
    char s[sizeof(obj)];
    memcpy(s, &obj, sizeof(s));
    return hash_rstring(sv_create_from_len(s, sizeof(s)));
}

static uint32_t hash_number(Number n)
{
    char s[sizeof(n)];
    memcpy(s, &n, sizeof(s));
    return hash_rstring(sv_create_from_len(s, sizeof(s)));
}

static void clear_entries(Entry *entries, int cap)
{
    for (int i = 0; i < cap; i++) {
        setv_nil(&entries[i].key);
        setv_nil(&entries[i].value);
    }
}

static uint32_t get_hash(const Value *t)
{
    switch (get_tag(t)) {
    case TYPE_NIL:      return 0; // WARNING: We should never hash `nil`!
    case TYPE_BOOLEAN:  return as_boolean(t);
    case TYPE_NUMBER:   return hash_number(as_number(t));
    case TYPE_STRING:   return as_string(t)->hash;
    case TYPE_TABLE:    return hash_pointer(as_object(t));
    }
}

// Find al free slot. Assumes there is at least 1 free slot left.
static Entry *find_entry(Entry *entries, int cap, const Value *k)
{
    uint32_t i    = get_hash(k) % cap;
    Entry   *tomb = NULL;
    for (;;) {
        Entry *ent = &entries[i];
        if (is_nil(&ent->key)) {
            if (is_nil(&ent->value))
                return (tomb == NULL) ? ent : tomb;
            if (tomb == NULL)
                tomb = ent;
        } else if (values_equal(&ent->key, k)) {
            return ent;
        }
        i = (i + 1) % cap;
    }
}

// Analogous to `adjustCapacity()` in the book. Assumes we only ever grow!
static void resize_table(Table *t, int newcap, Alloc *al)
{
    Entry *newbuf = new_parray(newbuf, newcap, al);
    clear_entries(newbuf, newcap);

    // Copy non-empty and non-tombstone entries to the new table.
    t->count = 0;
    for (int i = 0; i < t->cap; i++) {
        Entry *src = &t->entries[i];
        if (is_nil(&src->key))
            continue; // Throws away both empty and tombstone entries.

        Entry *dst = find_entry(newbuf, newcap, &src->key);
        dst->key   = src->key;
        dst->value = src->value;
        t->count++;
    }
    free_parray(t->entries, t->cap, al);
    t->entries = newbuf;
    t->cap     = newcap;
}

Table *new_table(int size, Alloc *al)
{
    Table *t = cast(Table*, new_object(sizeof(*t), TYPE_TABLE, al));
    init_table(t);
    if (size > 0)
        resize_table(t, size, al);
    return t;
}

void init_table(Table *t)
{
    t->entries = NULL;
    t->count   = 0;
    t->cap     = 0;
}

void free_table(Table *t, Alloc *al)
{
    free_parray(t->entries, t->cap, al);
    init_table(t);
}

bool get_table(Table *t, const Value *k, Value *out)
{
    if (t->count == 0 || is_nil(k)) {
        return false;
    }
    Entry *ent = find_entry(t->entries, t->cap, k);
    if (is_nil(&ent->key)) {
        return false;
    }
    *out = ent->value;
    return true;
}

bool set_table(Table *t, const Value *k, const Value *v, Alloc *al)
{
    if (is_nil(k))
        return false;
    if (t->count + 1 > t->cap * TABLE_MAX_LOAD)
        resize_table(t, grow_capacity(t->cap), al);

    Entry *ent      = find_entry(t->entries, t->cap, k);
    bool   isnewkey = is_nil(&ent->key);
    // Don't increase the count for tombstones (nil-key with non-nil value)
    if (isnewkey && is_nil(&ent->value))
        t->count++;
    ent->key   = *k;
    ent->value = *v;
    return isnewkey;
}

bool unset_table(Table *t, const Value *k)
{
    if (t->count == 0 || is_nil(k))
        return false;

    Entry *ent = find_entry(t->entries, t->cap, k);
    if (is_nil(&ent->key))
        return false;
    // Place a tombstone, it must be distinct from nil key with nil value.
    setv_nil(&ent->key);
    setv_boolean(&ent->value, false);
    return true;
}

void copy_table(Table *dst, const Table *src, Alloc *al)
{
    for (int i = 0; i < src->cap; i++) {
        const Entry *ent = &src->entries[i];
        if (is_nil(&ent->key)) {
            continue;
        }
        set_table(dst, &ent->key, &ent->value, al);
    }
}
