#pragma once

#include "value.hpp"

enum Metamethod {
    // Start of 'fast' metamethods
    MT_INDEX, MT_NEWINDEX, // Table access
    MT_EQ,
    MT_LEN,
    MT_GC,

    // Start of 'slow' metamethods
    MT_ADD, MT_SUB, MT_MUL, MT_DIV, MT_MOD, MT_POW, MT_UNM, // Arithmetic
    MT_LT, MT_LEQ, // Comparison
};

#define MT_COUNT    (MT_LEQ + 1)

// Prevent infinite recursion.
#define MT_MAX_LOOP 100

extern const char *const mt_names[MT_COUNT];

Value
mt_get_fast(lulu_VM *L, Table *mt, Metamethod m);

// Queries `getmetatable(v)[t]`.
Value
mt_get_method(lulu_VM *L, Value v, Metamethod t);
