#pragma once

#include "value.hpp"

enum Metamethod {
    MT_INDEX, MT_NEWINDEX, // Table access
    MT_ADD, MT_SUB, MT_MUL, MT_DIV, MT_MOD, MT_POW, MT_UNM, // Arithmetic
    MT_EQ, MT_LT, MT_LEQ, // Comparison
};

#define MT_COUNT    (MT_LEQ + 1)

// Prevent infinite recursion.
#define MT_MAX_LOOP 100

extern const char *const mt_names[MT_COUNT];

// Queries `getmetatable(v)[t]`.
Value
mt_get_method(lulu_VM *L, Value v, Metamethod t);
