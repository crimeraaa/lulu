#pragma once

#include "chunk.hpp"
#include "parser.hpp"

static constexpr u8 MAX_REG = OPCODE_MAX_A - 5;

struct Compiler {
    lulu_VM &vm;
    Parser  &parser; // All compilers share the same parser.
    Chunk   &chunk;  // Compilers do not own their chunks.
    u8       free_reg;
};

Compiler
compiler_make(lulu_VM &vm, Parser &p, Chunk &chunk);

int
compiler_code(Compiler &c, OpCode op, u8 a, u16 b, u16 c2, int line);

int
compiler_code(Compiler &c, OpCode op, u8 a, u32 bx, int line);


/**
 * @note 2025-06-16
 *  Assumptions:
 *  1.) If you need to push these `nil`s to registers, you should have reserved
 *      `n` registers beforehand. This function will not reserve for you.
 */
void
compiler_load_nil(Compiler &c, u8 reg, int n, int line);


/**
 * @note 2025-06-16
 *  Assumptions:
 *  1.) If you need to push the boolean to a register, you should have reserved
 *      it beforehand. This function will not reserve for you.
 */
void
compiler_load_boolean(Compiler &c, u8 reg, bool b, int line);

u32
compiler_add_constant(Compiler &c, Value v);

u32
compiler_add_constant(Compiler &c, Number n);

u32
compiler_add_constant(Compiler &c, OString *s);


/**
 * @brief
 *  -   Increments `c.free_reg` by `n`; asserting that it does not exceed
 *      `MAX_REG`.
 */
void
compiler_reserve_reg(Compiler &c, u16 n);


/**
 * @brief
 *  -   Unconditionally pushes `e` to the next free register.
 *  -   This is useful for stack-like semantics.
 *
 * @returns
 *  -   The register we pushed `e` to.
 */
u8
compiler_expr_next_reg(Compiler &c, Expr &e);


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
u16
compiler_expr_rk(Compiler &c, Expr &e);


/**
 * @brief
 *  -   If `e` is already discharged into a register, reuse it.
 *  -   Otherwise it is pushed to the next one.
 *  -   This is useful to recycle registers if `e` already has one.
 *
 * @returns
 *  -   The register `e` resides in.
 */
u8
compiler_expr_any_reg(Compiler &c, Expr &e);


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
void
compiler_code_arith(Compiler &c, OpCode op, Expr &left, Expr &right);

void
compiler_code_unary(Compiler &c, OpCode op, Expr &e);

void
compiler_code_compare(Compiler &c, OpCode op, bool cond, Expr &left, Expr &right);
