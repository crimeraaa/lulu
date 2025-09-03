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
    "for_in",      // OP_FOR_IN
    "call",        // OP_CALL
    "self",        // OP_SELF
    "closure",     // OP_CLOSURE
    "close",       // OP_CLOSE
    "return",      // OP_RETURN
};

static constexpr OpInfo
MAKE(OpFormat fmt, bool test, bool a, OpArg b, OpArg c = OPARG_UNUSED)
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

#define ABC  OPFORMAT_ABC
#define ABX  OPFORMAT_ABX
#define ASBX OPFORMAT_ASBX

/**
 * @note 2025-07-19
 *      ORDER: Keep in sync with `OpCode`!
 *
 * @details 2025-07-19
 *      Vim: '<,>'s/\v(OP_)(\w+),/[\1\2] = 0,/g
 */
const OpInfo opinfo[OPCODE_COUNT] = {
    //   fmt   test   a      b            c               | OpCode
    MAKE(ABC,  false, true,  OPARG_REGK),                // OP_MOVE
    MAKE(ABX,  false, true,  OPARG_REGK),                // OP_CONSTANT
    MAKE(ABC,  false, true,  OPARG_REGK),                // OP_NIL
    MAKE(ABC,  false, true,  OPARG_REGK,  OPARG_REGK),   // OP_BOOL
    MAKE(ABX,  false, true,  OPARG_REGK),                // OP_GET_GLOBAL
    MAKE(ABX,  false, false, OPARG_REGK),                // OP_SET_GLOBAL
    MAKE(ABC,  false, true,  OPARG_OTHER, OPARG_OTHER),  // OP_NEW_TABLE
    MAKE(ABC,  false, true,  OPARG_REGK,  OPARG_REGK),   // OP_GET_TABLE
    MAKE(ABC,  false, false, OPARG_REGK,  OPARG_REGK),   // OP_SET_TABLE
    MAKE(ABC,  false, true,  OPARG_OTHER, OPARG_OTHER),  // OP_SET_ARRAY
    MAKE(ABC,  false, true,  OPARG_REGK),                // OP_GET_UPVALUE
    MAKE(ABC,  false, false, OPARG_REGK),                // OP_SET_UPVALUE
    MAKE(ABC,  false, true,  OPARG_REGK,  OPARG_REGK),   // OP_ADD
    MAKE(ABC,  false, true,  OPARG_REGK,  OPARG_REGK),   // OP_SUB
    MAKE(ABC,  false, true,  OPARG_REGK,  OPARG_REGK),   // OP_MUL
    MAKE(ABC,  false, true,  OPARG_REGK,  OPARG_REGK),   // OP_DIV
    MAKE(ABC,  false, true,  OPARG_REGK,  OPARG_REGK),   // OP_MOD
    MAKE(ABC,  false, true,  OPARG_REGK,  OPARG_REGK),   // OP_POW
    MAKE(ABC,  true,  false, OPARG_REGK,  OPARG_REGK),   // OP_EQ
    MAKE(ABC,  true,  false, OPARG_REGK,  OPARG_REGK),   // OP_LT
    MAKE(ABC,  true,  false, OPARG_REGK,  OPARG_REGK),   // OP_LEQ
    MAKE(ABC,  false, true,  OPARG_REGK),                // OP_UNM
    MAKE(ABC,  false, true,  OPARG_REGK),                // OP_NOT
    MAKE(ABC,  false, true,  OPARG_REGK),                // OP_LEN
    MAKE(ABC,  false, true,  OPARG_REGK,  OPARG_REGK),   // OP_CONCAT
    MAKE(ABC,  true,  false, OPARG_UNUSED, OPARG_OTHER), // OP_TEST
    MAKE(ABC,  true,  true,  OPARG_REGK, OPARG_OTHER),   // OP_TEST_SET
    MAKE(ASBX, false, false, OPARG_JUMP),                // OP_JUMP
    MAKE(ASBX, true,  true,  OPARG_JUMP),                // OP_FOR_PREP
    MAKE(ASBX, true,  true,  OPARG_JUMP),                // OP_FOR_LOOP
    MAKE(ABC,  true,  false, OPARG_UNUSED, OPARG_REGK),  // OP_FOR_IN
    MAKE(ABC,  false, true,  OPARG_OTHER, OPARG_OTHER),  // OP_CALL
    MAKE(ABC,  false, true,  OPARG_REGK, OPARG_REGK),    // OP_SELF
    MAKE(ABX,  false, true,  OPARG_REGK),                // OP_CLOSURE
    MAKE(ABC,  false, false, OPARG_UNUSED),              // OP_CLOSE
    MAKE(ABC,  false, false, OPARG_OTHER),               // OP_RETURN
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
