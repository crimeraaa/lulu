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
hash_value(const Value *key)
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
find_pair(Pair *pairs, int cap, const Value *key)
{
    u32   dcap      = cast(u32)cap; // "dummy" cap for type safety.
    u32   index     = hash_value(key) % dcap;
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
        index = (index + 1) % dcap;
    }
    __builtin_unreachable();
}

static void
adjust_capacity(lulu_VM *vm, Table *table, int new_cap)
{
    Pair *new_pairs = array_new(Pair, vm, new_cap);
    for (int i = 0; i < new_cap; i++) {
        value_set_nil(&new_pairs[i].key);
        value_set_nil(&new_pairs[i].value);
    }

    Pair *old_pairs   = table->pairs;
    int   new_count_pairs = 0;
    int   old_cap     = table->cap;
    for (int i = 0; i < old_cap; i++) {
        Pair *src = &old_pairs[i];
        if (value_is_nil(&src->key)) {
            continue;
        }
        Pair *dst = find_pair(new_pairs, new_cap, &src->key);
        dst->key   = src->key;
        dst->value = src->value;
        new_count_pairs++;
    }

    array_free(Pair, vm, old_pairs, old_cap);
    table->pairs       = new_pairs;
    table->count_pairs = new_count_pairs;
    table->cap         = new_cap;
}

Table *
table_new(lulu_VM *vm, int count_hash, int count_array)
{
    Table *table = cast(Table *)object_new(vm, LULU_TYPE_TABLE, size_of(*table));
    table_init(table);
    // Clamp cap to a power of 2 so that we never modulo by 0 or 1.
    if (count_hash > 0) {
        adjust_capacity(vm, table, mem_grow_capacity(count_hash));
    }
    if (count_array > 0) {
        varray_reserve(vm, &table->array, mem_grow_capacity(count_array));
    }
    return table;
}

void
table_init(Table *self)
{
    varray_init(&self->array);
    self->pairs   = NULL;
    self->count_pairs = 0;
    self->cap     = 0;
}

void
table_free(lulu_VM *vm, Table *self)
{
    varray_free(vm, &self->array);
    array_free(Pair, vm, self->pairs, self->cap);
    table_init(self);
}

static const Value *
table_get_hash(Table *self, const Value *key)
{
    if (self->count_pairs == 0 || value_is_nil(key)) {
        return NULL;
    }
    Pair *pair = find_pair(self->pairs, self->cap, key);
    return value_is_nil(&pair->key) ? NULL : &pair->value;
}

const Value *
table_get(Table *self, const Value *key)
{
    // Try array segment
    int index;
    if (value_number_is_integer(key, &index)) {
        VArray *varray = &self->array;
        // Guaranteed to be zeroed out (i.e: all nils).
        if (1 <= index && index <= varray->cap) {
            return &varray->values[index - 1];
        }
        // Try hash segment
    }
    return table_get_hash(self, key);
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
    if (self->count_pairs == 0) {
        return NULL;
    }
    u32   cap   = cast(u32)self->cap;
    u32   index = hash % cap;
    Pair *pairs = self->pairs;
    for (;;) {
        Pair  *pair = &pairs[index];
        Value *key  = &pair->key;
        if (value_is_nil(key)) {
            // Stop if we find an empty non-tombstone entry.
            if (value_is_nil(&pair->value)) {
                return NULL;
            }
        } else if (value_is_string(key)) {
            OString *src = key->string;
            if (src->hash == hash && src->len == len) {
                if (memcmp(src->data, data, cast(usize)len) == 0) {
                    return src;
                }
            }
        }
        index = (index + 1) % cap;
    }
}

bool
table_set_hash(lulu_VM *vm, Table *self, const Value *key, const Value *value)
{
    if (self->count_pairs >= self->cap * LULU_TABLE_MAX_LOAD) {
        adjust_capacity(vm, self, mem_grow_capacity(self->cap));
    }

    Pair *pair = find_pair(self->pairs, self->cap, key);
    bool is_new_key = value_is_nil(&pair->key);
    if (is_new_key && value_is_nil(&pair->value)) {
        self->count_pairs++;
    }
    pair->key   = *key;
    pair->value = *value;
    return is_new_key;
}

static void
move_hash_to_array(lulu_VM *vm, Table *table, VArray *array, int start)
{
    for (int i = start; /* empty */; i++) {
        Value key;
        value_set_number(&key, cast(Number)i);
        const Value *value = table_get_hash(table, &key);
        if (!value || value_is_nil(value)) {
            break;
        }

        varray_write_at(vm, array, i - 1, value); // Lua index to C index
        table_unset(table, &key);
    }
}

void
table_set_array(lulu_VM *vm, Table *table, int index, const Value *value)
{
    VArray *array = &table->array;
    varray_write_at(vm, array, index - 1, value);

    /**
     * @note 2024-12-31:
     *      When inserting holes in the array, we actually keep it (the
     *      array) intact.
     *
     *      The only difference is that the 'len' is updated to reflect this
     *      change. This mainly applies when using the '#' operator.
     */
    if (value_is_nil(value)) {
        array->len = index - 1;
        return;
    }

    /**
     * Given:  hash  = {[2] = 'b'}
     *         index, value = 1, 'a'
     *         n_prev, n_next = 0, 1
     *
     * Result: array = {'a', 'b'}
     *         hash  = {};
     */
    move_hash_to_array(vm, table, array, index + 1);
}

bool
table_set(lulu_VM *vm, Table *self, const Value *key, const Value *value)
{
    int index;
    if (value_is_nil(key)) {
        return false;
    } else if (value_number_is_integer(key, &index)) {
        /**
         * We can directly write/append to the array segment.
         * Index 1 will ALWAYS go to the array segment.
         *
         * @note 2024-12-31:
         *      See: 'table_set_array' for why we use 'cap'.
         */
        if (1 <= index && index <= self->array.cap) {
            table_set_array(vm, self, index, value);
            return true;
        }
    }
    return table_set_hash(vm, self, key, value);
}

bool
table_unset(Table *self, const Value *key)
{
    if (self->count_pairs == 0 || value_is_nil(key)) {
        return false;
    }

    Pair *pair = find_pair(self->pairs, self->cap, key);

    // Already an empty key?
    if (value_is_nil(&pair->key)) {
        return false;
    }

    // Place a tombstone here.
    value_set_nil(&pair->key);
    value_set_boolean(&pair->value, true);
    return true;
}
