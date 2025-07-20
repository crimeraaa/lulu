#include "opcode.hpp"

/**
 * @note 2025-07-19:
 *  -   Keep in sync with `OpCode`!
 *
 * @details
 *  -   Vim: '<,>'s/\v(OP_)(\w+),/[\1\2] = "\L\2",/g
 */
const char *const opnames[OPCODE_COUNT] = {
    /* OP_CONSTANT */   "constant",
    /* OP_NIL */        "nil",
    /* OP_BOOL */       "bool",
    /* OP_GET_GLOBAL */ "get_global",
    /* OP_SET_GLOBAL */ "set_global",
    /* OP_NEW_TABLE */  "new_table",
    /* OP_GET_TABLE */  "get_table",
    /* OP_SET_TABLE */  "set_table",
    /* OP_MOVE */       "move",
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
MOVE_LIKE = UNARY;

/**
 * @note 2025--07-19
 *  -   ORDER: Keep in sync with `OpCode`!
 *
 * @details 2025-07-19
 *  -   Vim: '<,>'s/\v(OP_)(\w+),/[\1\2] = 0,/g
 */
const OpInfo opinfo[OPCODE_COUNT] = {
    /* OP_CONSTANT */   ABX(/* a */ true, OPARG_REGK),
    /* OP_NIL */        MOVE_LIKE,
    /* OP_BOOL */       ABC(/* test */ false, /* a */ true, OPARG_REGK, OPARG_REGK),
    /* OP_GET_GLOBAL */ ABX(/* a */ true, OPARG_REGK),
    /* OP_SET_GLOBAL */ ABX(/* a */ false, OPARG_REGK),
    /* OP_NEW_TABLE */  ABC(/* test */ false, /* a */ true, OPARG_OTHER, OPARG_OTHER),
    /* OP_GET_TABLE */  ARITH,
    /* OP_SET_TABLE */  ABC(/* test */ false, /* a */ false, OPARG_REGK, OPARG_REGK),
    /* OP_MOVE */       MOVE_LIKE,
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
    /* OP_CALL */       FUNC_LIKE,
    /* OP_RETURN */     FUNC_LIKE,
};
