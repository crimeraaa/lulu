#pragma once

#include "string.hpp"
#include "value.hpp"

struct LULU_PRIVATE Entry {
    Value key, value;
};

struct LULU_PRIVATE Table {
    OBJECT_HEADER;
    Slice<Entry> entries;
    isize        count;   // Number of currently active elements in `entries`.
};

LULU_FUNC Table *
table_new(lulu_VM *vm, isize n_hash, isize n_array);

LULU_FUNC void
table_init(Table *t);

LULU_FUNC bool
table_get(Table *t, const Value &restrict k, Value *restrict out);

LULU_FUNC void
table_set(lulu_VM *vm, Table *t, const Value &k, const Value &v);

LULU_FUNC void
table_unset(Table *t, const Value &k);

LULU_FUNC void
table_resize(lulu_VM *vm, Table *t, isize new_cap);

LULU_FUNC bool
table_next(lulu_VM *vm, Table *t, Value *restrict k, Value *restrict v);
