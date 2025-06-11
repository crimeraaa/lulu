#include "opcode.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wc99-designator"

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

static constexpr OpInfo
MAKE(OpFormat fmt, OpArg a, OpArg b, OpArg c)
{
    return cast(OpInfo,
        b << OPINFO_OFFSET_B
        |  c   << OPINFO_OFFSET_C
        |  a   << OPINFO_OFFSET_A
        |  fmt << OPINFO_OFFSET_FMT);
}

static constexpr OpInfo
ABC(OpArg a, OpArg b, OpArg c)
{
    return MAKE(OPFORMAT_ABC, a, b, c);
}

static constexpr OpInfo
ABX(OpArg bx)
{
    return MAKE(OPFORMAT_ABX, OPARG_REG, bx, OPARG_UNUSED);
}

static constexpr OpInfo
CONSTANT = ABX(OPARG_CONSTANT),
ARITH    = ABC(OPARG_REG, OPARG_REG_CONSTANT, OPARG_REG_CONSTANT);

// Vim: '<,>'s/\v(OP_)(\w+),/[\1\2] = 0,/g
const OpInfo opcode_info[OPCODE_COUNT] = {
    [OP_LOAD_CONSTANT] = CONSTANT,
    [OP_ADD]           = ARITH,
    [OP_SUB]           = ARITH,
    [OP_MUL]           = ARITH,
    [OP_DIV]           = ARITH,
    [OP_MOD]           = ARITH,
    [OP_POW]           = ARITH,
    [OP_RETURN]        = ABC(OPARG_REG, OPARG_ARGC, OPARG_TEST),
};

#pragma GCC diagnostic pop
