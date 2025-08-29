#pragma once

#include "string.hpp"
#include "value.hpp"

struct Entry {
    Value key, value;

    // Tombstones are always exactly `nil` keys mapping to `true`.
    void
    set_tombstone()
    {
        this->key   = nil;
        this->value = Value::make_boolean(true);
    }
};

struct Table : Object_Header {
    // This object is always independent, so it can be a root during
    // garbage collection.
    GC_List *gc_list;

    // Array segment data. Not all slots may be occupied.
    // `len(array)` is the functional capacity, not the active count.
    Slice<Value> array;

    // Hash segment data. Not all slots may be occupied.
    // `len(entries)` is the functional capacity, not the active count.
    Slice<Entry> entries;

    // Hash segment length- number of currently active entries.
    // These active entries need not be consecutive.
    isize count;
};

Table *
table_new(lulu_VM *L, isize n_hash, isize n_array);

void
table_delete(lulu_VM *L, Table *t);


/**
 * @param [out] v
 *      Will hold the result of `t[k]`.
 */
bool
table_get(Table *t, Value k, Value *v);

// Implements `t[k] = v`.
void
table_set(lulu_VM *L, Table *t, Value k, Value v);

// Implements `#t`.
isize
table_len(Table *t);

/**
 * @param [out] v
 *      Will hold the result of `t[i]`.
 */
bool
table_get_integer(Table *t, Integer i, Value *v);

// Implements `t[i] = v`.
void
table_set_integer(lulu_VM *L, Table *t, Integer i, Value v);

void
table_unset(Table *t, Value k);


/**
 * @brief
 *      Generic table iterator.
 *
 * @param [in, out] k
 *      Holds the current key of the iterator control variable, which
 *      is then set to the next key (or `nil`) upon return.
 *
 *      On the first iteration, must be `nil` to signal the start of the
 *      iteration. It will point to the first non-nil element in the
 *      underlying entry array.
 *
 *      Subsequent iterations will always make it point to the next non-nil
 *      entry in the entry array.
 *
 * @param [out] v
 *      Will hold the result of `t[k]`.
 *
 * @return
 *      `true` if iteration can start/continue, else `false`. In the falsy
 *      case `k` and `v` are not set so they may not be safe to read.
 */
bool
table_next(lulu_VM *L, Table *t, Value *restrict k, Value *restrict v);
