#pragma once

#include "small_array.hpp"
#include "chunk.hpp"
#include "parser.hpp"

static constexpr u16
NO_REG  = Instruction::MAX_A, // MUST fit in 8 bits for bit manipulation.
MAX_REG = Instruction::MAX_A - 5;

static constexpr isize
MAX_ACTIVE_LOCALS = 200,
MAX_TOTAL_LOCALS  = UINT16_MAX;

using Active_Array = Small_Array<u16, MAX_ACTIVE_LOCALS>;

struct LULU_PRIVATE Compiler {
    lulu_VM     *vm;
    Compiler    *prev;   // Have an enclosing function?
    Parser      *parser; // All compilers share the same parser.
    Chunk       *chunk;  // Compilers do not own their chunks.
    Table       *indexes; // Maps values to indexes in `chunk.constants`.
    isize        pc;     // Index of first free instruction: `len(chunk->code)`.
    isize        last_target;
    u16          free_reg;
    Active_Array active; // Indexes are local registers.
};

LULU_FUNC inline Instruction *
get_code(Compiler *c, isize pc)
{
    return &c->chunk->code[pc];
}

LULU_FUNC Compiler
compiler_make(lulu_VM *vm, Parser *p, Chunk *chunk, Table *indexes,
    Compiler *enclosing = nullptr);

[[noreturn]]
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

LULU_FUNC isize
compiler_code_asbx(Compiler *c, OpCode op, u16 a, i32 sbx, int line);


/**
 * @note(2025-06-16)
 *  Assumptions:
 *
 *  1.) If you need to push these `nil`s to registers, you should have
 *      reserved `n` registers beforehand. This function will not reserve
 *      for you.
 */
LULU_FUNC void
compiler_load_nil(Compiler *c, u16 reg, int n, int line);


/**
 * @note(2025-06-16)
 *  Assumptions:
 *
 *  1.) If you need to push the boolean to a register, you should have reserved
 *      it beforehand. This function will not reserve for you.
 */
LULU_FUNC void
compiler_load_boolean(Compiler *c, u16 reg, bool b, int line);

LULU_FUNC u32
compiler_add_number(Compiler *c, Number n);

LULU_FUNC u32
compiler_add_ostring(Compiler *c, OString *s);


/**
 * @brief
 *      Increments `c.free_reg` by `n`; asserting that it does not exceed
 *      `MAX_REG`.
 */
LULU_FUNC void
compiler_reserve_reg(Compiler *c, u16 n);


/**
 * @brief
 *      Unconditionally pushes `e` to the next free register. This is
 *      useful for stack-like semantics.
 *
 * @returns
 *      The register we pushed `e` to.
 */
LULU_FUNC u16
compiler_expr_next_reg(Compiler *c, Expr *e);


/**
 * @brief
 *      If `e` represents a literal or constant value, check if its index
 *      can fit in an RK register. This is useful to optimize away
 *      `OP_CONSTANT` for instructions that support it.
 *
 *      This will always save constant values to the constants array. If
 *      `e` does not fit in an RK register *or* it is not a constant value
 *      then we try to push it to a register (if it does not have one
 *      already).
 *
 * @returns
 *      The RK register of `e`, otherwise a normal register.
 */
LULU_FUNC u16
compiler_expr_rk(Compiler *c, Expr *e);


/**
 * @brief
 *      If `e` is already discharged into a register, reuse it. Otherwise
 *      it is pushed to the next one. This is useful to recycle registers
 *      if `e` already has one.
 *
 * @returns
 *      The register `e` resides in.
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
compiler_code_arith(Compiler *c, OpCode op, Expr *restrict left,
    Expr *restrict right);

LULU_FUNC void
compiler_code_unary(Compiler *c, OpCode op, Expr *e);

LULU_FUNC void
compiler_code_compare(Compiler *c, OpCode op, bool cond, Expr *restrict left,
    Expr *restrict right);

LULU_FUNC void
compiler_code_concat(Compiler *c, Expr *restrict left, Expr *restrict right);

LULU_FUNC void
compiler_code_return(Compiler *c, u16 reg, u16 count, bool is_vararg, int line);


/**
 * @note(2025-06-24)
 *      Analogous to `lcode.c:luaK_storevar(FuncState *fs, expdesc *var,
 *      expdesc *expr)` in Lua 5.1.5.
 */
LULU_FUNC void
compiler_set_variable(Compiler *c, Expr *restrict var, Expr *restrict expr);


// Does not convert `call` to `EXPR_DISCHARGED`.
LULU_FUNC void
compiler_set_returns(Compiler *c, Expr *call, u16 n);


/**
 * @brief
 *      If `e` represents an `OP_CALL`, then its argument C is set to 1 and
 *      `e` itself is converted to `EXPR_DISCHARGED` to signify it is final.
 *
 * @note 2025-06-24
 *      Analogous to `lcode.c:luaK_setoneret(FuncState *fs, expdesc *e)`
 *      in Lua 5.1.5.
 */
LULU_FUNC void
compiler_set_one_return(Compiler *c, Expr *e);


/**
 * @brief
 *      Finds the index of the `small_array_len(c->active) - 1` up to and
 *      including the `limit`-numbered local.
 *
 * @param limit
 *      The last index where we will check the `c->active` array.
 *      This is useful to enforce scoping rules, so that this is still
 *      valid: `local x; do local x; end;`
 *
 * @note(2025-07-10)
 *      If the local was not found, then `*reg` was not set. It is not safe
 *      to read in that case unless you initialized it beforehand.
 */
LULU_FUNC bool
compiler_get_local(Compiler *c, u16 limit, OString *ident, u16 *reg);

LULU_FUNC void
compiler_get_table(Compiler *c, Expr *restrict t, Expr *restrict k);


/**
 * @brief
 *      Unconditionally creates a new jump. That is, `sBx` is `-1` meaning
 *      this is the start (bottom) of the list.
 *
 * @return
 *      The `pc` of the new jump list. Use this to fill an `Expr`.
 */
LULU_FUNC isize
compiler_jump_new(Compiler *c, int line);


/**
 * @brief
 *      Chains a new jump to the jump list pointed at `*list`.
 *
 *      `*list` must be the start of a jump list, or it will be initialized
 *      to a new one.
 */
LULU_FUNC void
compiler_jump_add(Compiler *c, isize *list_pc, isize jump_pc);


/**
 * @brief
 *      This function is 'overloaded' to do the jobs of multiple separate
 *      functions from `lcode.c` in Lua 5.1.5.
 *
 *      1.) `luaK_patchtohere()` - leave `target` and `reg` to defaults.
 *          This is useful to patch a jump list to `c->pc` as in emulating
 *          `if` and `while` conditions.
 *
 *      2.) `luaK_patchlist()` - provide `target` but not `reg`.
 *          This is useful in emulating the unconditional jump of a `while`
 *          or a `for` loop.
 *
 *      3.) `lcode.c:patchlistaux()` - provide `target` and `reg`.
 *          This is only useful within `compiler.cpp`; it is how
 *          logical operators (along with their register allocations)
 *          are implemented.
 */
LULU_FUNC void
compiler_jump_patch(Compiler *c, isize jump_pc, isize target = NO_JUMP,
    u16 reg = NO_REG);


/**
 * @param cond
 *      When the resulting register of `left` as a boolean equals this,
 *      then the succeeding jump is skipped.
 *
 *      E.g. consider `if` statements and `and` expressions. If `left` is
 *      truthy, since `cond == true`, then the jump is skipped so that the
 *      block/right-hand expression is executed.
 *
 *      Otherwise, `right` is falsy when `cond == true`, so the jump is not
 *      skipped so we jump over the block/right-hand expression.
 *
 *  @note 2025-07-18
 *      We assume that the instruction before `OP_JUMP` is an instruction
 *      with a 'test' mode (e.g. `OP_TEST` or `OP_TEST_SET`) which may do
 *      `ip++` to skip over the jump.
 *
 * @note 2025-07-19
 *      Analogous to `lcode.c:luaK_goif{true,false}(FuncState *fs,
 *      expdesc *e)` in Lua 5.1.5.
 */
LULU_FUNC void
compiler_logical_new(Compiler *c, Expr *left, bool cond);

LULU_FUNC void
compiler_logical_patch(Compiler *c, Expr *restrict left, Expr *restrict right,
    bool cond);


/**
 * @brief
 *      Marks `c->pc` as a 'jump' target and returns it. This is useful to
 *      prevent bad optimizations when calling `compiler_code_nil()`.
 */
LULU_FUNC isize
compiler_label_get(Compiler *c);
