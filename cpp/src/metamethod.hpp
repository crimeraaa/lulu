#pragma once

#include "value.hpp"

enum Metamethod {
    MT_ADD, MT_SUB, MT_MUL, MT_DIV, MT_MOD, MT_POW, MT_UNM, // Arithmetic
    MT_EQ, MT_LT, MT_LEQ, // Comparison
};

#define MT_COUNT    (MT_LEQ + 1)

extern const char *const mt_names[MT_COUNT];

Value
mt_get_metatable(lulu_VM *L, Value v, Metamethod metamethod);
