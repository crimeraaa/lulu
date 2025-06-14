#pragma once

#include "chunk.hpp"
#include "parser.hpp"

static constexpr u8 MAX_REG = OPCODE_MAX_A - 5;

struct Compiler {
    lulu_VM &vm;
    Parser  &parser; // All compilers share the same parser.
    Chunk   &chunk;    // Compilers do not own their chunks.
    u8       free_reg;
};

Compiler
compiler_make(lulu_VM &vm, Parser &p, Chunk &chunk);

int
compiler_code(Compiler &c, OpCode op, u8 a, u16 b, u16 c2, int line);

int
compiler_code(Compiler &c, OpCode op, u8 a, u32 bx, int line);

u32
compiler_add_constant(Compiler &c, Value v);

u32
compiler_add_number(Compiler &c, Number n);

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

void
compiler_reserve_reg(Compiler &c, u16 n);

void
compiler_pop_reg(Compiler &c, u8 reg);

void
compiler_code_arith(Compiler &c, OpCode op, Expr &left, Expr &right);
