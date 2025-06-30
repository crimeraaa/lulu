#pragma once

#include "string.hpp"
#include "value.hpp"

struct Entry {
    Value key, value;
};

struct Table {
    OBJECT_HEADER;
    Slice<Entry> entries;
    size_t       count;   // Number of currently active elements in `entries`.
};

Table *
table_new(lulu_VM *vm, size_t n = 0);

void
table_init(Table *t);

bool
table_get(Table *t, Value k, Value *out);

void
table_set(lulu_VM *vm, Table *t, Value k, Value v);

void
table_unset(Table *t, Value k);

void
table_resize(lulu_VM *vm, Table *t, size_t new_cap);
