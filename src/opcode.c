#include "opcode.h"

// Vim:     '<,>'s/\v(OP_)(\w+),/[\1\2] = "\L\2",/g
const char *const opcode_names[OPCODE_COUNT] = {
    [OP_LOAD_CONSTANT] = "load_constant",
    [OP_ADD] = "add",
    [OP_SUB] = "sub",
    [OP_MUL] = "mul",
    [OP_DIV] = "div",
    [OP_MOD] = "mod",
    [OP_POW] = "pow",
    [OP_RETURN] = "return",
};

#define MAKE(fmt, a, b, c) \
    (\
        cast(OpInfo, b)     << OPINFO_OFFSET_B \
        | cast(OpInfo, c)   << OPINFO_OFFSET_C \
        | cast(OpInfo, a)   << OPINFO_OFFSET_A \
        | cast(OpInfo, fmt) << OPINFO_OFFSET_FMT \
    )

#define ABC(b, c) MAKE(OPFORMAT_ABC, OPARG_REG, b, c)
#define ABX(bx)   MAKE(OPFORMAT_ABX, OPARG_REG, bx, OPARG_UNUSED)
#define CONSTANT  ABX(OPARG_CONSTANT)
#define ARITH     ABC(OPARG_REG_CONSTANT, OPARG_REG_CONSTANT)

// Vim: '<,>'s/\v(OP_)(\w+),/[\1\2] = 0,/g
const OpInfo opcode_info[OPCODE_COUNT] = {
    [OP_LOAD_CONSTANT] = CONSTANT,
    [OP_ADD]           = ARITH,
    [OP_SUB]           = ARITH,
    [OP_MUL]           = ARITH,
    [OP_DIV]           = ARITH,
    [OP_MOD]           = ARITH,
    [OP_POW]           = ARITH,
    [OP_RETURN]        = ABC(OPARG_REG, OPARG_TEST),
};
