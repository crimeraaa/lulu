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

struct Table {
    OBJECT_HEADER;

    // Array segment data. Not all slots may be occupied.
    // `len(array)` is the functional capacity, not the active count.
    Slice<Value> array;

    // Hash segment data. Not all slots may be occupied.
    // `len(entries)` is the functional capacity, not the active count.
    Slice<Entry> entries;

    // Hash segment length- number of currently active entries.
    isize count;
};

Table *
table_new(lulu_VM *vm, isize n_hash, isize n_array);

void
table_delete(lulu_VM *vm, Table *t);

void
table_init(Table *t);

// Implements `out = t[k]`.
bool
table_get(Table *t, Value k, Value *out);

// Implements `t[k] = v`.
void
table_set(lulu_VM *vm, Table *t, Value k, Value v);

// Implements `#t`.
isize
table_len(Table *t);

// Implements `out = t[i]`.
bool
table_get_integer(Table *t, Integer i, Value *out);

// Implements `t[i] = v`.
void
table_set_integer(lulu_VM *vm, Table *t, Integer i, Value v);

void
table_unset(Table *t, Value k);


/**
 * @brief
 *      Generic table iterator.
 *
 * @param k
 *      Out-parameter for the iterator control variable.
 *
 *      On the first iteration, must be `nil` to signal the start of the
 *      iteration. It will point to the first non-nil element in the
 *      underlying entry array.
 *
 *      Subsequent iterations will always make it point to the next non-nil
 *      entry in the entry array.
 *
 * @param v
 *      Out-parameter for `t[k]`.
 *
 * @return
 *      `true` if iteration can start/continue, else `false`. In the falsy
 *      case `k` and `v` are not set so they may not be safe to read.
 */
bool
table_next(lulu_VM *vm, Table *t, Value *restrict k, Value *restrict v);
