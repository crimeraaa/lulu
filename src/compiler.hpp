#pragma once

#include "small_array.hpp"
#include "chunk.hpp"
#include "parser.hpp"

static constexpr u16
MAX_REG = OPCODE_MAX_A - 5;

static constexpr isize
MAX_ACTIVE_LOCALS = 200,
MAX_TOTAL_LOCALS  = UINT16_MAX;

struct Compiler {
    lulu_VM  *vm;
    Compiler *prev;   // Have an enclosing function?
    Parser   *parser; // All compilers share the same parser.
    Chunk    *chunk;  // Compilers do not own their chunks.
    u16       free_reg;
    Small_Array<u16, MAX_ACTIVE_LOCALS> active; // Indexes are local registers.
};

LULU_FUNC Compiler
compiler_make(lulu_VM *vm, Parser *p, Chunk *chunk, Compiler *enclosing = nullptr);

LULU_FUNC void
compiler_error_limit(Compiler *c, isize limit, const char *what);

template<class T>
LULU_FUNC inline void
compiler_check_limit(Compiler *c, T count, T limit, const char *what)
{
    if (count > limit) {
        compiler_error_limit(c, cast(isize)limit, what);
    }
}

LULU_FUNC isize
compiler_code_abc(Compiler *c, OpCode op, u16 a, u16 b, u16 c2, int line);

LULU_FUNC isize
compiler_code_abx(Compiler *c, OpCode op, u16 a, u32 bx, int line);


/**
 * @note 2025-06-16
 *  Assumptions:
 *  1.) If you need to push these `nil`s to registers, you should have reserved
 *      `n` registers beforehand. This function will not reserve for you.
 */
LULU_FUNC void
compiler_load_nil(Compiler *c, u16 reg, int n, int line);


/**
 * @note 2025-06-16
 *  Assumptions:
 *  1.) If you need to push the boolean to a register, you should have reserved
 *      it beforehand. This function will not reserve for you.
 */
LULU_FUNC void
compiler_load_boolean(Compiler *c, u16 reg, bool b, int line);

LULU_FUNC u32
compiler_add_value(Compiler *c, Value v);

LULU_FUNC u32
compiler_add_number(Compiler *c, Number n);

LULU_FUNC u32
compiler_add_ostring(Compiler *c, OString *s);


/**
 * @brief
 *  -   Increments `c.free_reg` by `n`; asserting that it does not exceed
 *      `MAX_REG`.
 */
LULU_FUNC void
compiler_reserve_reg(Compiler *c, u16 n);


/**
 * @brief
 *  -   Unconditionally pushes `e` to the next free register.
 *  -   This is useful for stack-like semantics.
 *
 * @returns
 *  -   The register we pushed `e` to.
 */
LULU_FUNC u16
compiler_expr_next_reg(Compiler *c, Expr *e);


/**
 * @brief
 *  -   If `e` represents a literal or constant value, check if its index can
 *      fit in an RK register.
 *  -   This is useful to optimize away `OP_CONSTANT` for instructions that
 *      support it.
 *  -   This will always save constant values to the constants array.
 *  -   If `e` does not fit in an RK register *or* it is not a constant value
 *      then we try to push it to a register (if it does not have one already).
 *
 * @returns
 *  -   The RK register of `e`, otherwise a normal register.
 */
LULU_FUNC u16
compiler_expr_rk(Compiler *c, Expr *e);


/**
 * @brief
 *  -   If `e` is already discharged into a register, reuse it.
 *  -   Otherwise it is pushed to the next one.
 *  -   This is useful to recycle registers if `e` already has one.
 *
 * @returns
 *  -   The register `e` resides in.
 */
LULU_FUNC u16
compiler_expr_any_reg(Compiler *c, Expr *e);


/**
 * @details 2025-06-16:
 *  -   Consider the expression `1 + 2*3`.
 *
 *  1.) We first parse `2*3`.
 *      -   Both are constants so they are directly encoded rather than
 *          pushed to registers first.
 *      -   The `Expr` holding `2`, the current `left`, is set to
 *          `RELOCABLE` holding the pc of `OP_MUL`.
 *      -   We pop both expressions, but since neither are in registers,
 *          nothing happens.
 *
 *  2.) We then parse `1 + <right>`.
 *      -   `left` is the `Expr` holding `1`.
 *      -   `right` is the `Expr` holding `OP_MUL` of constants `2` and `3`.
 *      -   We call `compiler_expr_rk()` on `right`, which eventually leads
 *          to `compiler_expr_next_reg()`.
 *      -   This transforms `right` to `DISCHARGED` holding the first free
 *          register, `0`. `c.free_reg` is now `1`.
 *      -   `left` meanwhile simply holds the constant.
 *      -   We pop both expressions, but only `right` is actually popped.
 *          `c.free_reg` is now `0`.
 */
LULU_FUNC void
compiler_code_arith(Compiler *c, OpCode op, Expr *left, Expr *right);

LULU_FUNC void
compiler_code_unary(Compiler *c, OpCode op, Expr *e);

LULU_FUNC void
compiler_code_compare(Compiler *c, OpCode op, bool cond, Expr *left, Expr *right);

LULU_FUNC void
compiler_code_concat(Compiler *c, Expr *left, Expr *right);

LULU_FUNC void
compiler_code_return(Compiler *c, u16 reg, u16 count, bool is_vararg, int line);


/**
 * @note 2025-06-24
 *  Analogous to:
 *  1.) `lcode.c:luaK_storevar(FuncState *fs, expdesc *var, expdesc *expr)`
 *      in Lua 5.1.5.
 */
LULU_FUNC void
compiler_set_variable(Compiler *c, Expr *var, Expr *expr);


// Does not convert `call` to `EXPR_DISCHARGED`.
LULU_FUNC void
compiler_set_returns(Compiler *c, Expr *call, u16 n);

/**
 * @brief
 *  -   If `e` represents an `OP_CALL`, then its argument C is set to 1 and
 *      `e` itself is converted to `EXPR_DISCHARGED` to signify it is final.
 *
 * @note 2025-06-24
 *  -   `lcode.c:luaK_setoneret(FuncState *fs, expdesc *e)` in Lua 5.1.5.
 */
LULU_FUNC void
compiler_set_one_return(Compiler *c, Expr *e);


/**
 * @brief
 *  -   Finds the index of the `len(c->active) - 1` up to and including `limit`
 *      numbered local.
 *
 * @param limit
 *  -   The last index where we will check the `c->active` array.
 *  -   This is useful to enforce scoping rules, so that this is still valid:
 *      `local x; do local x; end;`
 */
LULU_FUNC bool
compiler_get_local(Compiler *c, u16 limit, OString *id, u16 *reg);

LULU_FUNC void
compiler_get_table(Compiler *c, Expr *t, Expr *k);
