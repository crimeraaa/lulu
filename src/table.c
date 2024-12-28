/// local
#include "table.h"

/// standard
#include <string.h> // memcpy, memcmp

static u32
hash_number(Number number)
{
    char buf[size_of(number)];
    memcpy(buf, &number, size_of(number));
    return ostring_hash(buf, size_of(buf));
}

static u32
hash_pointer(const void *pointer)
{
    char buf[size_of(pointer)];
    memcpy(buf, &pointer, size_of(pointer));
    return ostring_hash(buf, size_of(buf));
}

static u32
get_hash(const Value *key)
{
    switch (key->type) {
    case LULU_TYPE_NIL:     return 0; // Should not happen!
    case LULU_TYPE_BOOLEAN: return cast(u32)key->boolean;
    case LULU_TYPE_NUMBER:  return hash_number(key->number);
    case LULU_TYPE_STRING:  return key->string->hash;
    case LULU_TYPE_TABLE:   return hash_pointer(key->table);
    }
}

// Find the pair referred to be key or the first empty pair.
static Pair *
find_pair(Pair *pairs, isize cap, const Value *key)
{
    u32   index     = get_hash(key) % cap;
    Pair *tombstone = NULL;
    for (;;) {
        Pair *pair = &pairs[index];
        // Empty key or tombstone?
        if (value_is_nil(&pair->key)) {
            // Currently empty?
            if (value_is_nil(&pair->value)) {
                return (tombstone) ? tombstone : pair;
            }
            // Check if we can reuse a tombstone.
            if (!tombstone) {
                tombstone = pair;
            }
        } else if (value_eq(&pair->key, key)) {
            return pair;
        }
        index = (index + 1) % cap;
    }
    __builtin_unreachable();
}

static void
adjust_capacity(lulu_VM *vm, Table *table, isize new_cap)
{
    Pair *new_pairs = array_new(Pair, vm, new_cap);
    for (isize i = 0; i < new_cap; i++) {
        value_set_nil(&new_pairs[i].key);
        value_set_nil(&new_pairs[i].value);
    }

    Pair *old_pairs = table->pairs;
    const isize      old_cap   = table->cap;
    table->count = 0;
    for (isize i = 0; i < old_cap; i++) {
        Pair *src = &old_pairs[i];
        if (value_is_nil(&src->key)) {
            continue;
        }
        Pair *dst = find_pair(new_pairs, new_cap, &src->key);
        dst->key   = src->key;
        dst->value = src->value;
        table->count++;
    }

    array_free(Pair, vm, old_pairs, old_cap);
    table->pairs = new_pairs;
    table->cap   = new_cap;
}

Table *
table_new(lulu_VM *vm, isize count)
{
    Table *table = cast(Table *)object_new(vm, LULU_TYPE_TABLE, size_of(*table));
    table_init(table);
    if (count > 0) {
        adjust_capacity(vm, table, count);
    }
    return table;
}

void
table_init(Table *self)
{
    self->pairs = NULL;
    self->count = 0;
    self->cap   = 0;
}

void
table_free(lulu_VM *vm, Table *self)
{
    array_free(Pair, vm, self->pairs, self->cap);
    table_init(self);
}

const Value *
table_get(Table *self, const Value *key)
{
    if (self->count == 0 || value_is_nil(key)) {
        return NULL;
    }
    Pair *pair = find_pair(self->pairs, self->cap, key);
    return value_is_nil(&pair->key) ? NULL : &pair->value;
}

OString *
table_intern_string(lulu_VM *vm, Table *self, OString *string)
{
    Value key;
    value_set_string(&key, string);
    table_set(vm, self, &key, &key);
    return string;
}

OString *
table_find_string(Table *self, const char *data, isize len, u32 hash)
{
    if (self->count == 0) {
        return NULL;
    }
    u32 index = hash % self->cap;
    for (;;) {
        Pair  *pair = &self->pairs[index];
        Value *key  = &pair->key;
        if (value_is_nil(key)) {
            // Stop if we find an empty non-tombstone entry.
            if (value_is_nil(&pair->value)) {
                return NULL;
            }
        } else if (value_is_string(key)) {
            OString *src = key->string;
            if (src->hash == hash && src->len == len) {
                if (memcmp(src->data, data, len) == 0) {
                    return src;
                }
            }
        }
        index = (index + 1) % self->cap;
    }
}

bool
table_set(lulu_VM *vm, Table *self, const Value *key, const Value *value)
{
    if (value_is_nil(key)) {
        return false;
    }
    if (self->count >= self->cap * LULU_TABLE_MAX_LOAD) {
        adjust_capacity(vm, self, mem_grow_capacity(self->cap));
    }

    Pair *pair = find_pair(self->pairs, self->cap, key);
    bool is_new_key = value_is_nil(&pair->key);
    if (is_new_key && value_is_nil(&pair->value)) {
        self->count++;
    }
    pair->key   = *key;
    pair->value = *value;
    return is_new_key;
}

bool
table_unset(Table *self, const Value *key)
{
    if (self->count == 0 || value_is_nil(key)) {
        return false;
    }

    Pair *pair = find_pair(self->pairs, self->cap, key);

    // Already an empty key?
    if (value_is_nil(&pair->key)) {
        return false;
    }

    // Place a tombstone here.
    pair->key   = LULU_VALUE_NIL;
    pair->value = LULU_VALUE_TRUE;
    return true;
}
