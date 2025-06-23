#pragma once

#include "object.hpp"
#include "string.hpp"

Table *
table_new(lulu_VM &vm, size_t n = 0);

void
table_init(Table &t);

Value
table_get(Table &t, Value k, bool &ok);

void
table_set(lulu_VM &vm, Table &t, Value k, Value v);

void
table_unset(Table &t, Value k);

void
table_resize(lulu_VM &vm, Table &t, size_t new_cap);
