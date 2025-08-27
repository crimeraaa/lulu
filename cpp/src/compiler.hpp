#pragma once

#include "chunk.hpp"
#include "parser.hpp"
#include "small_array.hpp"

static constexpr u16
    NO_REG  = Instruction::MAX_A, // MUST fit in 8 bits for bit manipulation.
    MAX_REG = NO_REG - 5;         // Must be less than `NO_REG`.

static constexpr int MAX_ACTIVE_LOCALS = 200, MAX_TOTAL_LOCALS = UINT16_MAX;

using Active_Array = Small_Array<u16, MAX_ACTIVE_LOCALS>;

struct Block {
    // Stack-allocated linked-list.
    Block *prev;

    // Jump list for all connected `break` statements, if `breakable`.
    int break_list;

    // Number of *initialized* locals at the *time of pushing*.
    u16 n_locals;

    // Is `break` valid for this block?
    bool breakable;

    // At least one upvalue was used by this function?
    bool has_upvalue;
};

struct Upvalue_Info {
    u16       data; // Local register or upvalue index.
    Expr_Type type; // local or upvalue?
};

constexpr int MAX_UPVALUES = UINT8_MAX;

using Upvalue_Info_Array = Small_Array<Upvalue_Info, MAX_UPVALUES>;

struct Compiler {
    lulu_VM *vm;

    // The enclosing (parent) function, non-`nullptr` if we are nested.
    // Helps in detecting and resolving upvalues.
    Compiler *prev;

    // All compilers in the same thread share the same parser.
    Parser *parser;

    // Compilers do not own their chunks; each closure is unique but holds
    // the same chunk.
    Chunk *chunk;

    // Map of constant values to indexes into `chunk->constants`.
    Table *indexes;

    // Stack-allocated linked list of blocks for local and upvalue
    // resulution.
    Block *block;

    // Used to help prevent marking of upvalues in non-breakable scopes.
    Block base_block;

    // Track information of all upvalues used by this function.
    Upvalue_Info_Array upvalues;

    // Indexes thereof are equivalent to local registers currently in use.
    // Values are the indexes into `chunk->locals` to be used for information.
    Active_Array active;

    // Index of the first free instruction, equivalent to `len(chunk->code)`.
    int pc;

    // Pc of the last potential jump target. Helps prevent bad
    // optimizations in compiler_load_nil().
    int last_target;
    u16 free_reg;
};

inline Instruction *
get_code(Compiler *c, int pc)
{
    return &c->chunk->code[pc];
}

Compiler
compiler_make(lulu_VM *vm, Parser *p, Chunk *f, Table *i, Compiler *prev);

[[noreturn]] void
compiler_error_limit(Compiler *c, int limit, const char *what);

inline void
compiler_check_limit(Compiler *c, int count, int limit, const char *what)
{
    if (count > limit) {
        compiler_error_limit(c, limit, what);
    }
}

int
compiler_code_abc(Compiler *c, OpCode op, u16 a, u16 b, u16 c2);

int
compiler_code_abx(Compiler *c, OpCode op, u16 a, u32 bx);

int
compiler_code_asbx(Compiler *c, OpCode op, u16 a, i32 sbx);


/**
 * @note(2025-06-16)
 *  Assumptions:
 *
 *  1.) If you need to push these `nil`s to registers, you should have
 *      reserved `n` registers beforehand. This function will not reserve
 *      for you.
 */
void
compiler_load_nil(Compiler *c, u16 reg, int n);


/**
 * @note(2025-06-16)
 *  Assumptions:
 *
 *  1.) If you need to push the boolean to a register, you should have reserved
 *      it beforehand. This function will not reserve for you.
 */
void
compiler_load_boolean(Compiler *c, u16 reg, bool b);

u32
compiler_add_number(Compiler *c, Number n);

u32
compiler_add_ostring(Compiler *c, OString *s);

u32
compiler_add_constant(Compiler *c, Value v);

void
compiler_check_stack(Compiler *c, u16 n);

/**
 * @brief
 *      Increments `c.free_reg` by `n`; asserting that it does not exceed
 *      `MAX_REG`.
 */
void
compiler_reserve_reg(Compiler *c, u16 n);


/**
 * @brief
 *      Unconditionally pushes `e` to the next free register. This is
 *      useful for stack-like semantics.
 *
 * @returns
 *      The register we pushed `e` to.
 */
u16
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
u16
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
u16
compiler_expr_any_reg(Compiler *c, Expr *e);


/**
 * @details 2025-06-16:
 *      Consider the expression `1 + 2*3`.
 *
 *  1.) We first parse `2*3`.
 *          Both are constants so they are directly encoded rather than
 *          pushed to registers first.
 *
 *          The `Expr` holding 2, the current `left`, is set to
 *          `RELOCABLE` holding the pc of `OP_MUL`.
 *
 *          We pop both expressions, but since neither are in registers,
 *          nothing happens.
 *
 *  2.) We then parse `1 + <right>`.
 *          `left` is the `Expr` holding 1. `right` is the `Expr` holding
 *          `OP_MUL` of constants 2 and 3.
 *
 *          We call `compiler_expr_rk()` on `right`, which eventually leads
 *          to `compiler_expr_next_reg()`. This transforms `right` to
 *          `DISCHARGED` holding the first free register, 0. `c.free_reg`
 *          is now 1.
 *
 *          `left` meanwhile simply holds the constant. We pop both
 *          expressions, but only `right` is actually popped. `c.free_reg`
 *          is now 0.
 */
void
compiler_code_arith(
    Compiler *c,
    OpCode    op,
    Expr *restrict left,
    Expr *restrict right
);

void
compiler_code_unary(Compiler *c, OpCode op, Expr *e);

void
compiler_code_compare(
    Compiler *c,
    OpCode    op,
    bool      cond,
    Expr *restrict left,
    Expr *restrict right
);

void
compiler_code_concat(Compiler *c, Expr *restrict left, Expr *restrict right);

void
compiler_code_return(Compiler *c, u16 reg, u16 count);


/**
 * @note(2025-06-24)
 *      Analogous to `lcode.c:luaK_storevar(FuncState *fs, expdesc *var,
 *      expdesc *expr)` in Lua 5.1.5.
 */
void
compiler_set_variable(Compiler *c, Expr *restrict var, Expr *restrict expr);


// Does not convert `call` to `EXPR_DISCHARGED`.
void
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
void
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
 * @return
 *      The register (which fits in 8 bits) if found, else `NO_REG`.
 */
u16
compiler_get_local(Compiler *c, u16 limit, OString *ident);

void
compiler_get_table(Compiler *c, Expr *restrict t, Expr *restrict k);

void
compiler_set_array(Compiler *c, u16 table_reg, isize n_array, isize to_store);


/**
 * @brief
 *      Unconditionally creates a new jump. That is, `sBx` is `-1` meaning
 *      this is the start (bottom) of the list.
 *
 * @return
 *      The `pc` of the new jump list. Use this to fill an `Expr`.
 */
int
compiler_jump_new(Compiler *c);


/**
 * @brief
 *      Chains a new jump to the jump list pointed at `*list`.
 *
 *      `*list` must be the start of a jump list, or it will be initialized
 *      to a new one.
 */
void
compiler_jump_add(Compiler *c, int *list_pc, int jump_pc);


/**
 * @brief
 *      This function is 'overloaded' to do the jobs of multiple separate
 *      functions from `lcode.c` in Lua 5.1.5.
 *
 *      1.) `luaK_patchtohere()`:
 *          Leave `target` and `reg` to defaults. This is useful to patch a
 *          jump list to `c->pc` for `if` and `while` conditions.
 *
 *      2.) `luaK_patchlist()`:
 *          Provide `target` but not `reg`. This is useful in emulating the
 *          unconditional jump of a `while` or a `for` loop.
 *
 *      3.) `lcode.c:patchlistaux()`
 *          Provide `target` and `reg`. This is only useful within
 *          `compiler.cpp`; it is how logical operators (along with their
 *          register allocations) are implemented.
 */
void
compiler_jump_patch(
    Compiler *c,
    int       jump_pc,
    int       target = NO_JUMP,
    u16       reg    = NO_REG
);


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
void
compiler_logical_new(Compiler *c, Expr *left, bool cond);

void
compiler_logical_patch(
    Compiler *c,
    Expr *restrict left,
    Expr *restrict right,
    bool cond
);


/**
 * @brief
 *      Marks `c->pc` as a 'jump' target and returns it. This is useful to
 *      prevent bad optimizations when calling `compiler_code_nil()`.
 */
int
compiler_label_get(Compiler *c);
