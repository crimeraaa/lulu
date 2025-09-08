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
        this->value.set_boolean(true);
    }
};

struct Table : Object_Header {
    // Bit set. 1 indicates metamethod is absent and 0 indicates present.
    u8 flags;

    // This object is always independent, so it can be a root during
    // garbage collection.
    GC_List *gc_list;

    // Used to lookup the basic metamethods (see metamethod.hpp).
    // Null by default; it must be explicitly constructed via setmetatable().
    Table *metatable;

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


/** @brief Get t[k].
 *
 * @param [out] key_exists
 *  If non-null, then holds true iff t[k] was non-nil beforehand else false.
 */
Value
table_get(Table *t, Value k, bool *key_exists = nullptr);


/** @brief Get a read-write pointer to `t[k]`. May trigger rehash. */
[[nodiscard]] Value *
table_set(lulu_VM *L, Table *t, Value k);


// Implements `#t`.
isize
table_len(Table *t);


// Value
// table_get_integer(Table *t, Integer i, bool *index_exists);


/** `table_set()`, but for array indices or integer keys. */
[[nodiscard]] Value *
table_set_integer(lulu_VM *L, Table *t, Integer i);


Value
table_get_string(Table *t, OString *k);

[[nodiscard]] Value *
table_set_string(lulu_VM *L, Table *t, OString *k);


// void
// table_unset(Table *t, Value k);


/** @brief Generic table iterator. Start the iteration with a `nil` key.
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
