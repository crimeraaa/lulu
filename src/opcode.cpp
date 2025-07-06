#include "opcode.hpp"

// Vim:     '<,>'s/\v(OP_)(\w+),/[\1\2] = "\L\2",/g
const char *const opcode_names[OPCODE_COUNT] = {
    /* OP_CONSTANT */   "constant",
    /* OP_LOAD_NIL */   "load_nil",
    /* OP_LOAD_BOOL */  "load_bool",
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
    /* OP_CONCAT */     "concat",
    /* OP_CALL */       "call",
    /* OP_RETURN */     "return",
};

static constexpr OpInfo
MAKE(OpFormat fmt, OpArg a, OpArg b, OpArg c)
{
    return OpInfo((b << OPINFO_OFFSET_B)
        |  (c   << OPINFO_OFFSET_C)
        |  (a   << OPINFO_OFFSET_A)
        |  (fmt << OPINFO_OFFSET_FMT));
}

static constexpr OpInfo
ABC(OpArg a, OpArg b, OpArg c)
{
    return MAKE(OPFORMAT_ABC, a, b, c);
}

static constexpr OpInfo
ABX(OpArg bx)
{
    return MAKE(OPFORMAT_ABX, OPARG_REGK, bx, OPARG_UNUSED);
}

static constexpr OpInfo
CONSTANT  = ABX(OPARG_REGK),
ARITH     = ABC(OPARG_REGK, OPARG_REGK, OPARG_REGK),
COMPARE   = ARITH,
UNARY     = ABC(OPARG_REGK, OPARG_REGK, OPARG_UNUSED),
FUNC_LIKE = ABC(OPARG_REGK, OPARG_OTHER, OPARG_OTHER),
MOVE_LIKE = UNARY;

// Vim: '<,>'s/\v(OP_)(\w+),/[\1\2] = 0,/g
const OpInfo opcode_info[OPCODE_COUNT] = {
    /* OP_CONSTANT */   CONSTANT,
    /* OP_LOAD_NIL */   MOVE_LIKE,
    /* OP_LOAD_BOOL */  ABC(OPARG_REGK, OPARG_OTHER, OPARG_UNUSED),
    /* OP_GET_GLOBAL */ CONSTANT,
    /* OP_SET_GLOBAL */ CONSTANT,
    /* OP_NEW_TABLE */  ABC(OPARG_REGK, OPARG_OTHER, OPARG_OTHER),
    /* OP_GET_TABLE */  ARITH,
    /* OP_SET_TABLE */  ARITH,
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
    /* OP_CONCAT */     ABC(OPARG_REGK, OPARG_REGK,   OPARG_REGK),
    /* OP_CALL */       FUNC_LIKE,
    /* OP_RETURN */     FUNC_LIKE,
};
