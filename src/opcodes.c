#include "opcodes.h"

// See: https://www.lua.org/source/5.1/lopcodes.c.html#luaP_opnames
const char *const luaP_opnames[] = {
    [OP_RETURN] = "OP_RETURN",
    NULL,
};

#define opmode(tag, ra, rb, rc, mode) \
    (((tag) << 7) | ((ra) << 6) | ((rb) << 4) | ((rc) << 2) | (mode))

// See: https://www.lua.org/source/5.1/lopcodes.c.html#luaP_opmodes
const Byte luaP_opmodes[] = {
    [OP_RETURN] = opmode(0, 0, OpArgU, OpArgN, iABC),
};

