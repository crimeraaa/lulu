#include "table.h"

#include <string.h>

lulu_Table *
lulu_Table_new(lulu_VM *vm)
{
    lulu_Table *table = cast(lulu_Table *)lulu_Object_new(vm, LULU_TYPE_TABLE, size_of(lulu_Table));
    lulu_Table_init(table);
    return table;
}

void
lulu_Table_init(lulu_Table *self)
{
    self->pairs = NULL;
    self->count = 0;
    self->cap   = 0;
}

void
lulu_Table_free(lulu_VM *vm, lulu_Table *self)
{
    rawarray_free(lulu_Table_Pair, vm, self->pairs, self->cap);
    lulu_Table_init(self);
}

static u32
hash_number(lulu_Number number)
{
    char buf[size_of(number)];
    memcpy(buf, &number, size_of(number));
    return lulu_String_hash(buf, size_of(buf));
}

static u32
hash_pointer(const void *pointer)
{
    char buf[size_of(pointer)];
    memcpy(buf, &pointer, size_of(pointer));
    return lulu_String_hash(buf, size_of(buf));
}

static u32
get_hash(const lulu_Value *key)
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
static lulu_Table_Pair *
find_pair(lulu_Table_Pair *pairs, isize cap, const lulu_Value *key)
{
    u32 index = get_hash(key) % cap;
    lulu_Table_Pair *tombstone = NULL;
    for (;;) {
        lulu_Table_Pair *pair = &pairs[index];
        // Empty key or tombstone?
        if (lulu_Value_is_nil(&pair->key)) {
            // Currently empty?
            if (lulu_Value_is_nil(&pair->value)) {
                return (tombstone) ? tombstone : pair;
            }
            // Check if can reuse a tombstone.
            if (!tombstone) {
                tombstone = pair;
            }
        } else if (lulu_Value_eq(&pair->key, key)) {
            return pair;
        }
        index = (index + 1) % cap;
    }
    __builtin_unreachable();
}

static void
adjust_capacity(lulu_VM *vm, lulu_Table *table, isize new_cap)
{
    lulu_Table_Pair *new_pairs = rawarray_new(lulu_Table_Pair, vm, new_cap);
    for (isize i = 0; i < new_cap; i++) {
        lulu_Value_set_nil(&new_pairs[i].key);
        lulu_Value_set_nil(&new_pairs[i].value);
    }
    
    lulu_Table_Pair *old_pairs = table->pairs;
    const isize      old_cap   = table->cap;
    table->count = 0;
    for (isize i = 0; i < old_cap; i++) {
        lulu_Table_Pair *src = &old_pairs[i];
        if (lulu_Value_is_nil(&src->key)) {
            continue;
        }
        lulu_Table_Pair *dst = find_pair(new_pairs, new_cap, &src->key);
        dst->key   = src->key;
        dst->value = src->value;
        table->count++;
    }

    rawarray_free(lulu_Table_Pair, vm, old_pairs, old_cap);
    table->pairs = new_pairs;
    table->cap   = new_cap;
}

lulu_Value
lulu_Table_get(lulu_Table *self, const lulu_Value *key)
{
    if (self->count == 0 || lulu_Value_is_nil(key)) {
        return LULU_VALUE_NIL;
    }
    return find_pair(self->pairs, self->cap, key)->value;
}

lulu_String *
lulu_Table_intern_string(lulu_VM *vm, lulu_Table *self, lulu_String *string)
{
    lulu_Value key;
    lulu_Value_set_string(&key, string);
    lulu_Table_set(vm, self, &key, &key);
    return string;
}

lulu_String *
lulu_Table_find_string(lulu_Table *self, const char *data, isize len, u32 hash)
{
    if (self->count == 0) {
        return NULL;
    }
    u32 index = hash % self->cap;
    for (;;) {
        lulu_Table_Pair *pair = &self->pairs[index];
        lulu_Value      *key  = &pair->key;
        if (lulu_Value_is_nil(key)) {
            // Stop if we find an empty non-tombstone entry.
            if (lulu_Value_is_nil(&pair->value)) {
                return NULL;
            }
        } else if (lulu_Value_is_string(key)) {
            lulu_String *src = key->string;
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
lulu_Table_set(lulu_VM *vm, lulu_Table *self, const lulu_Value *key, const lulu_Value *value)
{
    if (self->count >= self->cap * LULU_TABLE_MAX_LOAD) {
        adjust_capacity(vm, self, GROW_CAPACITY(self->cap));
    }
    lulu_Table_Pair *pair = find_pair(self->pairs, self->cap, key);

    bool is_new_key = lulu_Value_is_nil(&pair->key);
    if (is_new_key && lulu_Value_is_nil(&pair->value)) {
        self->count++;
    }
    pair->key   = *key;
    pair->value = *value;
    return is_new_key;
}

bool
lulu_Table_unset(lulu_Table *self, const lulu_Value *key)
{
    if (self->count == 0 || lulu_Value_is_nil(key)) {
        return false;
    }
    
    lulu_Table_Pair *pair = find_pair(self->pairs, self->cap, key);

    // Already an empty key?
    if (lulu_Value_is_nil(&pair->key)) {
        return false;
    }

    // Place a tombstone here.
    pair->key   = LULU_VALUE_NIL;
    pair->value = LULU_VALUE_TRUE;
    return true;
}
