#include "opcode.hpp"

/**
 * @note 2025-07-19:
 *      ORDER: Keep in sync with `OpCode`!
 *
 * @details
 *      Vim: '<,>'s/\v(OP_)(\w+),/[\1\2] = "\L\2",/g
 */
const char *const opnames[OPCODE_COUNT] = {
    "move",        // OP_MOVE
    "constant",    // OP_CONSTANT
    "nil",         // OP_NIL
    "bool",        // OP_BOOL
    "get_global",  // OP_GET_GLOBAL
    "set_global",  // OP_SET_GLOBAL
    "new_table",   // OP_NEW_TABLE
    "get_table",   // OP_GET_TABLE
    "set_table",   // OP_SET_TABLE
    "set_array",   // OP_SET_ARRAY
    "get_upvalue", // OP_GET_UPVALUE
    "set_upvalue", // OP_SET_UPVALUE
    "add",         // OP_ADD
    "sub",         // OP_SUB
    "mul",         // OP_MUL
    "div",         // OP_DIV
    "mod",         // OP_MOD
    "pow",         // OP_POW
    "eq",          // OP_EQ
    "lt",          // OP_LT
    "leq",         // OP_LEQ
    "unm",         // OP_UNM
    "not",         // OP_NOT
    "len",         // OP_LEN
    "concat",      // OP_CONCAT
    "test",        // OP_TEST
    "test_set",    // OP_TEST_SET
    "jump",        // OP_JUMP
    "for_prep",    // OP_FOR_PREP
    "for_loop",    // OP_FOR_LOOP
    "for_in_loop", // OP_FOR_IN_LOOP
    "call",        // OP_CALL
    "closure",     // OP_CLOSURE
    "return",      // OP_RETURN
};

static constexpr OpInfo
MAKE(OpFormat fmt, bool test, bool a, OpArg b, OpArg c)
{
    // Unsure why, but each `x << y` here results in type `int` regardless of
    // the cast. Yet this is not true in `Instruction::make_*()`.
    auto n = (static_cast<u8>(test) << OpInfo::OFFSET_TEST)
             | (static_cast<u8>(a) << OpInfo::OFFSET_A)
             | (static_cast<u8>(b) << OpInfo::OFFSET_B)
             | (static_cast<u8>(c) << OpInfo::OFFSET_C)
             | (static_cast<u8>(fmt) << OpInfo::OFFSET_FMT);

    return {static_cast<u8>(n)};
}

static constexpr OpInfo
ABC(bool test, bool a, OpArg b, OpArg c)
{
    return MAKE(OPFORMAT_ABC, test, a, b, c);
}

static constexpr OpInfo
ABX(bool a, OpArg bx)
{
    return MAKE(OPFORMAT_ABX, /* test */ false, a, bx, OPARG_UNUSED);
}

static constexpr OpInfo
ASBX(bool test, bool a, OpArg b, OpArg c)
{
    return MAKE(OPFORMAT_ASBX, test, a, b, c);
}

static constexpr OpInfo
    ARITH       = ABC(/*test=*/false, /*a=*/true, OPARG_REGK, OPARG_REGK),
    COMPARE     = ABC(/*test=*/true, /*a=*/false, OPARG_REGK, OPARG_REGK),
    UNARY       = ABC(/*test=*/false, /*a=*/true, OPARG_REGK, OPARG_UNUSED),
    FUNC_LIKE   = ABC(/*test=*/false, /*a=*/true, OPARG_OTHER, OPARG_OTHER),
    MOVE_LIKE   = UNARY,
    FOR_LIKE    = ASBX(/*test=*/true, /*a=*/true, OPARG_JUMP, OPARG_UNUSED),
    FOR_IN_LIKE = ABC(/*test=*/true, /*a=*/false, OPARG_UNUSED, OPARG_REGK);

/**
 * @note 2025-07-19
 *      ORDER: Keep in sync with `OpCode`!
 *
 * @details 2025-07-19
 *      Vim: '<,>'s/\v(OP_)(\w+),/[\1\2] = 0,/g
 */
const OpInfo opinfo[OPCODE_COUNT] = {
    /* OP_MOVE */ MOVE_LIKE,
    /* OP_CONSTANT */ ABX(/*a=*/true, OPARG_REGK),
    /* OP_NIL */ MOVE_LIKE,
    /* OP_BOOL */ ABC(/*test=*/false, /*a=*/true, OPARG_REGK, OPARG_REGK),
    /* OP_GET_GLOBAL */ ABX(/*a=*/true, OPARG_REGK),
    /* OP_SET_GLOBAL */ ABX(/*a=*/false, OPARG_REGK),
    /* OP_NEW_TABLE */ FUNC_LIKE,
    /* OP_GET_TABLE */ ARITH,
    /* OP_SET_TABLE */
    ABC(/* test */ false, /* a */ false, OPARG_REGK, OPARG_REGK),
    /* OP_SET_ARRAY */ FUNC_LIKE,
    /* OP_GET_UPVALUE */ MOVE_LIKE,
    /* OP_SET_UPVALUE */ MOVE_LIKE,
    /* OP_ADD */ ARITH,
    /* OP_SUB */ ARITH,
    /* OP_MUL */ ARITH,
    /* OP_DIV */ ARITH,
    /* OP_MOD */ ARITH,
    /* OP_POW */ ARITH,
    /* OP_EQ */ COMPARE,
    /* OP_LT */ COMPARE,
    /* OP_LEQ */ COMPARE,
    /* OP_UNM */ UNARY,
    /* OP_NOT */ UNARY,
    /* OP_LEN */ UNARY,
    /* OP_CONCAT */ ARITH,
    /* OP_TEST */ ABC(/*test=*/true, /*a=*/false, OPARG_UNUSED, OPARG_OTHER),
    /* OP_TEST_SET */ ABC(/*test=*/true, /*a=*/true, OPARG_REGK, OPARG_OTHER),
    /* OP_JUMP */ ASBX(/*test=*/false, /*a=*/false, OPARG_JUMP, OPARG_UNUSED),
    /* OP_FOR_PREP */ FOR_LIKE,
    /* OP_FOR_LOOP */ FOR_LIKE,
    /* OP_FOR_IN_LOOP */ FOR_IN_LIKE,
    /* OP_CALL */ FUNC_LIKE,
    /* OP_CLOSURE */ ABX(true, OPARG_REGK),
    /* OP_RETURN */ ABC(/*test=*/false, /*a=*/false, OPARG_OTHER, OPARG_UNUSED),
};

static constexpr unsigned int
    // 1-bits in 0b0000_0111
    FB_MANT_SIZE = 3,

    // 0b0000_1000
    FB_MANT_IMPLIED = 1 << FB_MANT_SIZE,

    // 0b0000_0111
    FB_MANT_MASK = FB_MANT_IMPLIED - 1,

    // 0B0000_1111
    FB_MANT_IMPLIED_MAX = FB_MANT_IMPLIED | FB_MANT_MASK,

    FB_EXP_SIZE = 5,

    // 0b0001_1111
    FB_EXP_MASK = (1 << FB_EXP_SIZE) - 1;

u16
floating_byte_make(isize x)
{
    u16 exp = 0;

    // Even with implied bit, value is too large. Need a nonzero exponent.
    while (x > FB_MANT_IMPLIED_MAX) {
        // + 1 may toggle the rightmost bit, and >> 1 propagates it.
        // This approximates a value larger than the original which is fine.
        x = (x + 1) >> 1;
        exp++;
    }

    // Don't need to use implied bit? If we did in this case we would end up
    // with negative values for the mantissa which complicates things.
    if (x < FB_MANT_IMPLIED) {
        return static_cast<u16>(x);
    }

    // We have an exponent (which may be 0), shift it into position.
    // Add 1 to differentiate from 0 which indicates to decode mantissa as-is.
    exp      = (exp + 1) << FB_MANT_SIZE;
    u16 mant = static_cast<u16>(x - FB_MANT_IMPLIED);
    return exp | mant;
}

isize
floating_byte_decode(u16 fbyte)
{
    u16 exp = (fbyte >> FB_MANT_SIZE) & FB_EXP_MASK;

    // Just decode mantissa as-is?
    if (exp == 0) {
        return static_cast<isize>(fbyte);
    }

    u16 mant = (fbyte & FB_MANT_MASK) + FB_MANT_IMPLIED;

    // Subtract 1 from the exponent because we previously added 1 to
    // differentiate from the above case.
    return static_cast<isize>(mant) << (exp - 1);
}
