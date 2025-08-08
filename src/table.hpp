#pragma once

#include "string.hpp"
#include "value.hpp"

struct LULU_PRIVATE Entry {
    Value key, value;
};

struct LULU_PRIVATE Table {
    OBJECT_HEADER;

    // Hash segment data.
    Slice<Entry> entries;

    // Hash segment length- number of currently active entries.
    isize count;
};

LULU_FUNC Table *
table_new(lulu_VM *vm, isize n_hash, isize n_array);

LULU_FUNC void
table_init(Table *t);

// Implements `out = t[k]`.
LULU_FUNC bool
table_get(Table *t, const Value &restrict k, Value *restrict out);

// Implements `t[k] = v`.
LULU_FUNC void
table_set(lulu_VM *vm, Table *t, const Value &k, const Value &v);

// Implements `#t`.
LULU_FUNC isize
table_len(Table *t);

// Implements `out = t[i]`.
LULU_FUNC bool
table_get_integer(Table *t, lulu_Integer i, Value *out);

// Implements `t[i] = v`.
LULU_FUNC void
table_set_integer(lulu_VM *vm, Table *t, lulu_Integer i, const Value &v);

LULU_FUNC void
table_unset(Table *t, const Value &k);


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
LULU_FUNC bool
table_next(lulu_VM *vm, Table *t, Value *restrict k, Value *restrict v);
