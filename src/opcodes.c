#include "opcodes.h"

// See: https://www.lua.org/source/5.1/lopcodes.c.html#luaP_opnames
const char *const luaP_opnames[] = {
    [OP_CONSTANT]   = "OP_CONSTANT",
    [OP_RETURN]     = "OP_RETURN",
    NULL,
};

#define opmode(test, ra, rb, rc, mode) \
    (((test) << 7) | ((ra) << 6) | ((rb) << 4) | ((rc) << 2) | (mode))

// See: https://www.lua.org/source/5.1/lopcodes.c.html#luaP_opmodes
const Byte luaP_opmodes[] = {
    // OPCODE                TEST   R(A)    R(B)        R(C)        OPMODE
    [OP_CONSTANT]   = opmode(0,     1,      OpArgK,     OpArgN,     iABx),
    [OP_RETURN]     = opmode(0,     0,      OpArgU,     OpArgN,     iABC),
};

