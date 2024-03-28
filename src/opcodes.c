#include "opcodes.h"

// See: https://www.lua.org/source/5.1/lopcodes.c.html#luaP_opnames
const char *const luaP_opnames[] = {
    [OP_CONSTANT]   = "OP_CONSTANT",
    [OP_ADD]        = "OP_ADD",
    [OP_SUB]        = "OP_SUB",
    [OP_MUL]        = "OP_MUL",
    [OP_DIV]        = "OP_DIV",
    [OP_MOD]        = "OP_MOD",
    [OP_POW]        = "OP_POW",
    [OP_UNM]        = "OP_UNM",
    [OP_RETURN]     = "OP_RETURN",
    NULL,
};

#define opmode(test, ra, rb, rc, mode) \
    (((test) << 7) | ((ra) << 6) | ((rb) << 4) | ((rc) << 2) | (mode))

const Byte luaP_opmodes[NUM_OPCODES] = {
    // OPCODE                TEST   R(A)    R(B)        R(C)        OPMODE
    [OP_CONSTANT]   = opmode(0,     1,      OpArgK,     OpArgN,     iABx),
    [OP_ADD]        = opmode(0,     1,      OpArgK,     OpArgK,     iABC),
    [OP_SUB]        = opmode(0,     1,      OpArgK,     OpArgK,     iABC),
    [OP_MUL]        = opmode(0,     1,      OpArgK,     OpArgK,     iABC),
    [OP_DIV]        = opmode(0,     1,      OpArgK,     OpArgK,     iABC),
    [OP_MOD]        = opmode(0,     1,      OpArgK,     OpArgK,     iABC),
    [OP_POW]        = opmode(0,     1,      OpArgK,     OpArgK,     iABC),
    [OP_UNM]        = opmode(0,     1,      OpArgR,     OpArgN,     iABC),
    [OP_RETURN]     = opmode(0,     0,      OpArgU,     OpArgN,     iABC),
};

