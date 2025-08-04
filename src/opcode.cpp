#include "opcode.hpp"

/**
 * @note 2025-07-19:
 *  -   Keep in sync with `OpCode`!
 *
 * @details
 *  -   Vim: '<,>'s/\v(OP_)(\w+),/[\1\2] = "\L\2",/g
 */
const char *const opnames[OPCODE_COUNT] = {
    /* OP_MOVE */       "move",
    /* OP_CONSTANT */   "constant",
    /* OP_NIL */        "nil",
    /* OP_BOOL */       "bool",
    /* OP_GET_GLOBAL */ "get_global",
    /* OP_SET_GLOBAL */ "set_global",
    /* OP_NEW_TABLE */  "new_table",
    /* OP_GET_TABLE */  "get_table",
    /* OP_SET_TABLE */  "set_table",
    /* OP_SET_ARRAY */  "set_array",
    /* OP_ADD */        "add",
    /* OP_SUB */        "sub",
    /* OP_MUL */        "mul",
    /* OP_DIV */        "div",
    /* OP_MOD */        "mod",
    /* OP_POW */        "pow",
    /* OP_EQ */         "eq",
    /* OP_LT */         "lt",
    /* OP_LEQ */        "leq",
    /* OP_UNM */        "unm",
    /* OP_NOT */        "not",
    /* OP_LEN */        "len",
    /* OP_CONCAT */     "concat",
    /* OP_TEST */       "test",
    /* OP_TEST_SET */   "test_set",
    /* OP_JUMP */       "jump",
    /* OP_FOR_PREP */   "for_prep",
    /* OP_FOR_LOOP */   "for_loop",
    /* OP_CALL */       "call",
    /* OP_RETURN */     "return",
};

static constexpr OpInfo
MAKE(OpFormat fmt, bool test, bool a, OpArg b, OpArg c)
{
    // Unsure why, but each `x << y` here results in type `int` regardless of
    // the cast. Yet this is not true in `Instruction::make_*()`.
    auto n = (cast(u8)test << OpInfo::OFFSET_TEST)
          | (cast(u8)a    << OpInfo::OFFSET_A)
          | (cast(u8)b    << OpInfo::OFFSET_B)
          | (cast(u8)c    << OpInfo::OFFSET_C)
          | (cast(u8)fmt  << OpInfo::OFFSET_FMT);

    return {cast(u8)n};
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
ARITH     = ABC(/* test */ false, /* a */ true,  OPARG_REGK,  OPARG_REGK),
COMPARE   = ABC(/* test */ true,  /* a */ false, OPARG_REGK,  OPARG_REGK),
UNARY     = ABC(/* test */ false, /* a */ true,  OPARG_REGK,  OPARG_UNUSED),
FUNC_LIKE = ABC(/* test */ false, /* a */ true,  OPARG_OTHER, OPARG_OTHER),
MOVE_LIKE = UNARY,
FOR_LIKE  = MAKE(OPFORMAT_ASBX, /* test */ true, /* a */ true, OPARG_JUMP, OPARG_UNUSED);

/**
 * @note 2025--07-19
 *  -   ORDER: Keep in sync with `OpCode`!
 *
 * @details 2025-07-19
 *  -   Vim: '<,>'s/\v(OP_)(\w+),/[\1\2] = 0,/g
 */
const OpInfo opinfo[OPCODE_COUNT] = {
    /* OP_MOVE */       MOVE_LIKE,
    /* OP_CONSTANT */   ABX(/* a */ true, OPARG_REGK),
    /* OP_NIL */        MOVE_LIKE,
    /* OP_BOOL */       ABC(/* test */ false, /* a */ true, OPARG_REGK, OPARG_REGK),
    /* OP_GET_GLOBAL */ ABX(/* a */ true, OPARG_REGK),
    /* OP_SET_GLOBAL */ ABX(/* a */ false, OPARG_REGK),
    /* OP_NEW_TABLE */  ABC(/* test */ false, /* a */ true, OPARG_OTHER, OPARG_OTHER),
    /* OP_GET_TABLE */  ARITH,
    /* OP_SET_TABLE */  ABC(/* test */ false, /* a */ false, OPARG_REGK, OPARG_REGK),
    /* OP_SET_ARRAY */  ABC(/* test */ false, /* a */ true, OPARG_OTHER, OPARG_OTHER),
    /* OP_ADD */        ARITH,
    /* OP_SUB */        ARITH,
    /* OP_MUL */        ARITH,
    /* OP_DIV */        ARITH,
    /* OP_MOD */        ARITH,
    /* OP_POW */        ARITH,
    /* OP_EQ */         COMPARE,
    /* OP_LT */         COMPARE,
    /* OP_LEQ */        COMPARE,
    /* OP_UNM */        UNARY,
    /* OP_NOT */        UNARY,
    /* OP_LEN */        UNARY,
    /* OP_CONCAT */     ARITH,
    /* OP_TEST */       ABC(/* test */ true, /* a */ false, OPARG_UNUSED, OPARG_OTHER),
    /* OP_TEST_SET */   ABC(/* test */ true, /* a */ true,  OPARG_REGK,   OPARG_OTHER),
    /* OP_JUMP */       MAKE(OPFORMAT_ASBX, /* test */ false, /* a */ false, OPARG_JUMP, OPARG_UNUSED),
    /* OP_FOR_PREP */   FOR_LIKE,
    /* OP_FOR_LOOP */   FOR_LIKE,
    /* OP_CALL */       FUNC_LIKE,
    /* OP_RETURN */     FUNC_LIKE,
};

static constexpr unsigned int
FB_MANT_SIZE        = 3,                                // 1-bits in 0b0000_0111
FB_MANT_IMPLIED     = 1 << FB_MANT_SIZE,                // 0b0000_1000
FB_MANT_MASK        = FB_MANT_IMPLIED - 1,              // 0b0000_0111
FB_MANT_IMPLIED_MAX = FB_MANT_IMPLIED | FB_MANT_MASK,   // 0B0000_1111
FB_EXP_SIZE         = 5,
FB_EXP_MASK         = (1 << FB_EXP_SIZE) - 1;           // 0b0001_1111

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
        return cast(u16)x;
    }

    // We have an exponent (which may be 0), shift it into position.
    // Add 1 to differentiate from 0 which indicates to decode mantissa as-is.
    exp = (exp + 1) << FB_MANT_SIZE;
    u16 mant = cast(u16)(x - FB_MANT_IMPLIED);
    return exp | mant;
}

isize
floating_byte_decode(u16 fbyte)
{
    u16 exp = (fbyte >> FB_MANT_SIZE) & FB_EXP_MASK;

    // Just decode mantissa as-is?
    if (exp == 0) {
        return cast_isize(fbyte);
    }

    u16 mant = (fbyte & FB_MANT_MASK) + FB_MANT_IMPLIED;

    // Subtract 1 from the exponent because we previously added 1 to
    // differentiate from the above case.
    return cast_isize(mant) << cast_isize(exp - 1);
}
