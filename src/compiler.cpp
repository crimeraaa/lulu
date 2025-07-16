#include <stdio.h>  // sprintf

#include "compiler.hpp"
#include "vm.hpp"

Compiler
compiler_make(lulu_VM *vm, Parser *p, Chunk *chunk, Compiler *enclosing)
{
    Compiler c;
    c.vm       = vm;
    c.prev     = enclosing;
    c.parser   = p;
    c.chunk    = chunk;
    c.free_reg = 0;
    small_array_init(&c.active);
    return c;
}

void
compiler_error_limit(Compiler *c, isize limit, const char *what, const Token *where)
{
    const char *who = (c->prev == nullptr) ? "script" : "function";
    char buf[128];
    sprintf(buf, "%s uses more than %" ISIZE_FMTSPEC " %s", who, limit, what);
    parser_error_at(c->parser, where, buf);
}

//=== BYTECODE MANIPULATION ================================================ {{{

isize
compiler_code_abc(Compiler *c, OpCode op, u16 a, u16 b, u16 c2, int line)
{
    lulu_assert(opinfo[op].a() && a <= Instruction::MAX_A || a == 0);
    lulu_assert(opinfo[op].b() == OPARG_REGK || opinfo[op].b() == OPARG_OTHER || b == 0);
    lulu_assert(opinfo[op].c() == OPARG_REGK || opinfo[op].c() == OPARG_OTHER || c2 == 0);

    return chunk_append(c->vm, c->chunk, Instruction::make_abc(op, a, b, c2), line);
}

isize
compiler_code_abx(Compiler *c, OpCode op, u16 a, u32 bx, int line)
{
    lulu_assert(opinfo[op].a() && a <= Instruction::MAX_A || a == 0);
    lulu_assert(opinfo[op].b() == OPARG_REGK || opinfo[op].b() == OPARG_OTHER);
    lulu_assert(opinfo[op].c() == OPARG_UNUSED);
    return chunk_append(c->vm, c->chunk, Instruction::make_abx(op, a, bx), line);
}

void
compiler_load_nil(Compiler *c, u16 reg, int n, int line)
{
    isize pc = len(c->chunk->code);
    // Stack frame is initialized to all `nil` at the start of the function, so
    // nothing to do.
    if (pc == 0) {
        return;
    }

    Instruction *ip = get_code(c, pc - 1);
    u16 last_reg = reg + cast(u16)(n - 1);
    // Previous instruction may be able to be folded?
    if (ip->op() == OP_LOAD_NIL) {
        u16 ra = ip->a();
        u16 rb = ip->b();

        // New argument B could be used to update the previous?
        if (ra <= last_reg && last_reg > rb) {
            ip->set_b(last_reg);
            return;
        }
    }

    // Can't fold; need new instruction.
    compiler_code_abc(c, OP_LOAD_NIL, reg, last_reg, 0, line);
}

void
compiler_load_boolean(Compiler *c, u16 reg, bool b, int line)
{
    compiler_code_abc(c, OP_LOAD_BOOL, reg, cast(u16)b, 0, line);
}

u32
compiler_add_value(Compiler *c, Value v)
{
    return chunk_add_constant(c->vm, c->chunk, v);
}

u32
compiler_add_number(Compiler *c, Number n)
{
    return compiler_add_value(c, n);
}

u32
compiler_add_ostring(Compiler *c, OString *s)
{
    return compiler_add_value(c, value_make_string(s));
}

//=== }}} ======================================================================

//=== REGISTER MANIPULATION ================================================ {{{

void
compiler_reserve_reg(Compiler *c, u16 n)
{
    c->free_reg += n;
    compiler_check_limit(c, c->free_reg, MAX_REG, "registers");
    if (c->chunk->stack_used < c->free_reg) {
        c->chunk->stack_used = c->free_reg;
    }
}

static void
pop_reg(Compiler *c, u16 reg)
{
    // `reg` is not a constant index nor a local register?
    if (!Instruction::reg_is_rk(reg) && reg >= cast(u16)small_array_len(c->active)) {
        c->free_reg -= 1;
        // e.g. if we discharged 1 number, free_reg would be 1 and the expr.reg
        // would be 0. So when we pop that number from its register, we expect
        // to see free_reg == 0 again.
        lulu_assertf(c->free_reg == reg, "Register %u cannot be popped", reg);
    }
}

static void
pop_expr(Compiler *c, const Expr *e)
{
    if (e->type == EXPR_DISCHARGED) {
        pop_reg(c, e->reg);
    }
}

/**
 * @brief
 *  -   Emits bytecode needed for variable retrieval (global or local), or
 *      table field retrieval.
 *
 * @note 2025-07-16
 *  -   In such case `e` is transformed to `EXPR_RELOCABLE` but its destination
 *      register is not yet set.
 */
static void
discharge_vars(Compiler *c, Expr *e, int line)
{
    switch (e->type) {
    case EXPR_GLOBAL:
        e->type = EXPR_RELOCABLE;
        e->pc   = compiler_code_abx(c, OP_GET_GLOBAL, NO_REG, e->index, line);
        break;
    case EXPR_LOCAL:
        // locals are already in registers
        e->type = EXPR_DISCHARGED;
        break;
    case EXPR_INDEXED: {
        u16 t = e->table.reg;
        u16 k = e->table.field_rk;
        e->type = EXPR_RELOCABLE;
        e->pc   = compiler_code_abc(c, OP_GET_TABLE, NO_REG, t, k, line);
        // We can reuse these registers as they're no longer needed (for now).
        pop_reg(c, k);
        pop_reg(c, t);
        break;
    }
    case EXPR_CALL:
        compiler_set_one_return(c, e);
        break;
    default:
        break;
    }
}

static void
discharge_to_reg(Compiler *c, Expr *e, u16 reg, int line)
{
    discharge_vars(c, e, line);
    switch (e->type) {
    case EXPR_NIL:
        compiler_load_nil(c, reg, 1, line);
        break;
    case EXPR_FALSE:
    case EXPR_TRUE: // fall-through
        compiler_load_boolean(c, reg, e->type == EXPR_TRUE, line);
        break;
    case EXPR_NUMBER: {
        u32 i = compiler_add_number(c, e->number);
        compiler_code_abx(c, OP_CONSTANT, reg, i, line);
        break;
    }
    case EXPR_CONSTANT: {
        compiler_code_abx(c, OP_CONSTANT, reg, e->index, line);
        break;
    }
    case EXPR_RELOCABLE: {
        Instruction *ip = get_code(c, e->pc);
        ip->set_a(reg);
        break;
    }
    case EXPR_DISCHARGED: {
        if (e->reg != reg) {
            compiler_code_abc(c, OP_MOVE, reg, e->reg, 0, line);
        }
        break;
    }
    default:
        lulu_assertf(e->type == EXPR_NONE || e->type == EXPR_JUMP,
            "Expr_Type(%i) not yet implemented", e->type);
        lulu_unreachable();
        return;
    }
    e->type = EXPR_DISCHARGED;
    e->reg  = reg;
}

static u16
discharge_any_reg(Compiler *c, Expr *e, int line)
{
    if (e->type == EXPR_DISCHARGED) {
        return e->reg;
    }
    u16 reg = c->free_reg;
    compiler_reserve_reg(c, 1);
    discharge_to_reg(c, e, reg, line);
    return reg;
}

// Get the `pc` of the relative target in `jump_pc`, or `NO_JUMP`.
static isize
get_jump(Compiler *c, isize jump_pc)
{
    Instruction i = *get_code(c, jump_pc);
    lulu_assert(i.op() == OP_JUMP);
    i32 offset = i.sbx();
    if (offset == NO_JUMP) {
        return NO_JUMP;
    }
    return (jump_pc + 1) + cast_isize(offset);
}

static isize
isize_abs(isize i)
{
    return (i >= 0) ? i : -i;
}

static void
set_jump(Compiler *c, isize jump_pc, isize target_pc)
{
    Instruction *ip = get_code(c, jump_pc);
    lulu_assert(ip->op() == OP_JUMP);

    isize offset = target_pc - (jump_pc + 1);
    lulu_assert(offset != NO_JUMP);

    compiler_check_limit(c, isize_abs(offset), cast_isize(Instruction::MAX_SBX),
        "jump offset");

    ip->set_sbx(cast(i32)offset);
}

static void
expr_to_reg(Compiler *c, Expr *e, u16 reg, int line)
{
    discharge_to_reg(c, e, reg, line);
    if (expr_has_jumps(e)) {
        isize pc = (e->patch_true == NO_JUMP) ? e->patch_false : e->patch_true;
        compiler_jump_patch(c, pc, NO_JUMP, reg);
    }
    *e = expr_make_reg(EXPR_DISCHARGED, reg, line);
}

u16
compiler_expr_next_reg(Compiler *c, Expr *e)
{
    int line = e->line;
    discharge_vars(c, e, line);
    pop_expr(c, e);

    u16 reg = c->free_reg;
    compiler_reserve_reg(c, 1);
    expr_to_reg(c, e, reg, line);
    return reg;
}

u16
compiler_expr_any_reg(Compiler *c, Expr *e)
{
    discharge_vars(c, e, e->line);
    if (e->type == EXPR_DISCHARGED) {
        if (!expr_has_jumps(e)) {
            return e->reg;
        }
        lulu_assertln(false, "Expr with jumps not yet implemented");
    }
    return compiler_expr_next_reg(c, e);
}

static u16
value_to_rk(Compiler *c, Expr *e, Value v)
{
    u32 i    = compiler_add_value(c, v);
    e->type  = EXPR_CONSTANT;
    e->index = i;

    // `i` can be encoded directly into an RK register?
    if (i <= Instruction::MAX_RK) {
        // Return the bit-toggled index.
        return Instruction::reg_to_rk(i);
    }
    // Can't fit in an RK register.
    return compiler_expr_any_reg(c, e);
}

u16
compiler_expr_rk(Compiler *c, Expr *e)
{
    switch (e->type) {
    case EXPR_NIL:    return value_to_rk(c, e, nil);
    case EXPR_FALSE:  return value_to_rk(c, e, false);
    case EXPR_TRUE:   return value_to_rk(c, e, true);
    case EXPR_NUMBER: return value_to_rk(c, e, e->number);

    // May reach here if we previously called this.
    case EXPR_CONSTANT: {
        if (e->index <= Instruction::MAX_RK) {
            return Instruction::reg_to_rk(cast(u16)e->index);
        }
        break;
    }
    default:
        break;
    }
    // If already discharged, don't push it again.
    return compiler_expr_any_reg(c, e);
}

//=== }}} ======================================================================

static bool
folded_arith(OpCode op, Expr *left, const Expr *right)
{
    // At least one argument is not a number literal?
    if (left->type != EXPR_NUMBER || right->type != EXPR_NUMBER) {
        return false;
    }

    Number a, b, n;
    a = left->number;
    b = right->number;
    switch (op) {
    case OP_ADD: n = lulu_Number_add(a, b); break;
    case OP_SUB: n = lulu_Number_sub(a, b); break;
    case OP_MUL: n = lulu_Number_mul(a, b); break;
    case OP_DIV:
        // Do not divide by 0.
        if (b == 0) {
            return false;
        }
        n = lulu_Number_div(a, b);
        break;
    case OP_MOD:
        // Do not divide by 0.
        if (b == 0) {
            return false;
        }
        n = lulu_Number_mod(a, b);
        break;
    case OP_POW: n = lulu_Number_pow(a, b); break;
    default:
        lulu_unreachable();
        return false;
    }
    left->number = n;
    return true;
}

void
compiler_code_arith(Compiler *c, OpCode op, Expr *left, Expr *right)
{
    lulu_assert((OP_ADD <= op && op <= OP_POW) || op == OP_CONCAT);
    if (folded_arith(op, left, right)) {
        return;
    }

    u16 rkc = compiler_expr_rk(c, right);
    u16 rkb = compiler_expr_rk(c, left);

    if (rkc > rkb) {
        pop_expr(c, right);
        pop_expr(c, left);
    } else {
        pop_expr(c, left);
        pop_expr(c, right);
    }

    left->type = EXPR_RELOCABLE;
    left->pc   = compiler_code_abc(c, op, NO_REG, rkb, rkc, left->line);
}

void
compiler_code_unary(Compiler *c, OpCode op, Expr *e)
{
    lulu_assert(OP_UNM <= op && op <= OP_NOT);

    switch (op) {
    case OP_UNM:
        // Constant folding
        if (expr_is_number(e)) {
            e->number = lulu_Number_unm(e->number);
            return;
        }
        break;
    case OP_NOT:
        // Constant folding
        switch (e->type) {
        case EXPR_NIL:
        case EXPR_FALSE:
            e->type = EXPR_TRUE;
            return;
        case EXPR_TRUE:
        case EXPR_NUMBER:
        case EXPR_CONSTANT:
            e->type = EXPR_FALSE;
            return;
        default:
            break;
        }
        break;
    default:
        lulu_unreachable();
        break;
    }

    // Unary minus and unary `not` cannot operate on RK registers.
    u16 rb = compiler_expr_next_reg(c, e);
    pop_expr(c, e);

    e->type = EXPR_RELOCABLE;
    e->pc   = compiler_code_abc(c, op, NO_REG, rb, 0, e->line);
}

static void
expr_bool(Expr *e, bool b)
{
    e->type = (b) ? EXPR_TRUE : EXPR_FALSE;
}

static bool
folded_compare(OpCode op, bool cond, Expr *left, Expr *right)
{
    bool result;
    if (op == OP_EQ) {
        if (left->type != right->type && !expr_is_literal(left)) {
            return false;
        }

        switch (left->type) {
        case EXPR_NIL:      // fall-through
        case EXPR_TRUE:     // fall-through
        case EXPR_FALSE:    result = true; break;
        case EXPR_NUMBER:   result = lulu_Number_eq(left->number, right->number); break;
        case EXPR_CONSTANT: return false; // To be safe, must be a runtime op.
        default:
            lulu_unreachable();
            break;
        }

        if (!cond) {
            result = !result;
        }
    } else {
        if (!expr_is_number(left) || !expr_is_number(right)) {
            return false;
        }
        Number a, b;
        if (cond) {
            a = left->number;
            b = right->number;
        } else {
            a = right->number;
            b = left->number;
        }
        result = (op == OP_LT) ? lulu_Number_lt(a, b) : lulu_Number_leq(a, b);
    }

    expr_bool(left, result);
    return true;
}

void
compiler_code_compare(Compiler *c, OpCode op, bool cond, Expr *left, Expr *right)
{
    lulu_assert(OP_EQ <= op && op <= OP_LEQ);
    if (folded_compare(op, cond, left, right)) {
        return;
    }
    u16 rkc = compiler_expr_rk(c, right);
    u16 rkb = compiler_expr_rk(c, left);

    if (rkc > rkb) {
        pop_expr(c, right);
        pop_expr(c, left);
    } else {
        pop_expr(c, left);
        pop_expr(c, right);
    }

    // Switch order of encoded arguments so simulate greater than/equal-to.
    // `left > right`  <=> `right < left`
    // `left >= right` <=> `right <= left`
    if (!cond && op != OP_EQ) {
        swap(&rkb, &rkc);
    }

    left->type = EXPR_RELOCABLE;
    left->pc   = compiler_code_abc(c, op, NO_REG, rkb, rkc, left->line);

    // Switching the order of arguments changes nothing in equals. To simulate
    // not-equals, we need to flip the result of an equals.
    if (!cond && op == OP_EQ) {
        compiler_code_unary(c, OP_NOT, left);
    }
}

void
compiler_code_concat(Compiler *c, Expr *left, Expr *right)
{
    /**
     * @details 2025-06-22
     *  -   Checks if we are able to fold consecutive concats.
     *  -   Folding is impossible with left-associavity due to argument pushing.
     *      If `a..b..c` were parsed as `(a..b)..c`, then `(a..b)` would push
     *      `a` and `b` emit `OP_CONCAT`. By the time we reach `c`, there is
     *      nothing we can do to fold because the bytecode is already set.
     *  -   `a..(b..c)` is better because we would have pushed `a`, `b` and `c`
     *      by the time we emit `OP_CONCAT`.
     */
    if (right->type == EXPR_RELOCABLE) {
        Instruction *ip = get_code(c, right->pc);
        if (ip->op() == OP_CONCAT) {
            lulu_assert(left->reg == ip->b() - 1);
            pop_expr(c, left);
            ip->set_b(left->reg);
            left->type = EXPR_RELOCABLE;
            left->pc   = right->pc;
            return;
        }
    }
    // Don't put `right` in an RK register when in `compiler_code_arith()`.
    compiler_expr_next_reg(c, right);
    compiler_code_arith(c, OP_CONCAT, left, right);
}

void
compiler_set_variable(Compiler *c, Expr *var, Expr *expr)
{
    switch (var->type) {
    case EXPR_GLOBAL: {
        u16 reg = compiler_expr_any_reg(c, expr);
        compiler_code_abx(c, OP_SET_GLOBAL, reg, var->index, var->line);
        break;
    }
    case EXPR_LOCAL: {
        // Pop if temporary register.
        pop_expr(c, expr);

        // Set destination register or code `OP_MOVE`.
        expr_to_reg(c, expr, var->reg, var->line);
        break;
    }
    case EXPR_INDEXED: {
        u16 t = var->table.reg;
        u16 k = var->table.field_rk;
        u16 v = compiler_expr_rk(c, expr);
        compiler_code_abc(c, OP_SET_TABLE, t, k, v, var->line);
        break;
    }
    default:
        lulu_assertf(false, "Invalid Expr_Type(%i) to assign", var->type);
        lulu_unreachable();
        break;
    }
    pop_expr(c, expr);
}

void
compiler_code_return(Compiler *c, u16 reg, u16 count, bool is_vararg, int line)
{
    compiler_code_abc(c, OP_RETURN, reg, count, cast(u16)is_vararg, line);
}

void
compiler_set_returns(Compiler *c, Expr *call, u16 n)
{
    if (call->type == EXPR_CALL) {
        Instruction *ip = get_code(c, call->pc);
        ip->set_c(n);
    }
}

void
compiler_set_one_return(Compiler *c, Expr *e)
{
    if (e->type == EXPR_CALL) {
        Instruction *ip = get_code(c, e->pc);
        ip->set_c(1);
        e->type = EXPR_DISCHARGED;
        e->reg  = ip->a();
    }
}

bool
compiler_get_local(Compiler *c, u16 limit, OString *id, u16 *reg)
{
    for (isize r = small_array_len(c->active) - 1; r >= cast_isize(limit); r--) {
        Local *local = &c->chunk->locals[small_array_get(c->active, r)];
        if (local->identifier == id) {
            *reg = cast(u16)r;
            return true;
        }
    }
    return false;
}

void
compiler_get_table(Compiler *c, Expr *t, Expr *k)
{
    u16 rkb = compiler_expr_rk(c, k);
    t->type           = EXPR_INDEXED;
    t->table.reg      = t->reg;
    t->table.field_rk = rkb;
}

isize
compiler_jump_new(Compiler *c, int line)
{
    Instruction i = Instruction::make_asbx(OP_JUMP, 0, NO_JUMP);
    return chunk_append(c->vm, c->chunk, i, line);
}

void
compiler_jump_add(Compiler *c, isize *list_pc, isize jump_pc)
{
    // Nothing to do?
    if (jump_pc == NO_JUMP) {
        return;
    }
    // No list yet?
    else if (*list_pc == NO_JUMP) {
        *list_pc = jump_pc;
        return;
    }

    // `*pc` points to the start of the jump list, so loop for the first
    // `OP_JUMP` with `sBx == NO_JUMP`.
    isize pc = *list_pc;
    for (;;) {
        isize next = get_jump(c, pc);
        if (next == NO_JUMP) {
            set_jump(c, pc, jump_pc);
            break;
        }
        pc = next;
    }
}

static Instruction *
get_jump_control(Compiler *c, isize jump_pc)
{
    Instruction *ip = get_code(c, jump_pc);
    lulu_assert(ip->op() == OP_JUMP);
    if (jump_pc >= 1) {
        Instruction *ctrl = ip - 1;
        // only `OP_TEST{SET}`
        if (opinfo[ctrl->op()].test()) {
            return ctrl;
        }
    }
    return ip;
}

static bool
test_reg_patch(Compiler *c, isize jump_pc, u16 reg)
{
    Instruction *ip = get_jump_control(c, jump_pc);
    if (ip->op() != OP_TEST_SET) {
        return false;
    }

    u16 rb = ip->b();
    bool cond = cast(bool)ip->c();
    if (reg != NO_REG && reg != rb) {
        ip->set_a(reg);
    } else {
        // Destination register isn't needed or `rb` is already the destination.
        *ip = Instruction::make_abc(OP_TEST, rb, 0, cond);
    }
    return true;
}

void
compiler_jump_patch(Compiler *c, isize jump_pc, isize target, u16 reg)
{
    if (jump_pc == NO_JUMP) {
        return;
    }

    Slice<Instruction> code = slice_slice(c->chunk->code);
    if (target == NO_JUMP) {
        // Do not subtract 1; we want to skip over the latest instruction.
        target = len(code);
    }

    lulu_assertln(jump_pc != target, "Likely infinite loop");
    for (;;) {
        isize next = get_jump(c, jump_pc);
        test_reg_patch(c, jump_pc, reg);
        set_jump(c, jump_pc, target);
        if (next == NO_JUMP) {
            break;
        }

        jump_pc = next;
    }
}

static isize
get_logical_target(Compiler *c, Expr *e, bool cond)
{
    int line = e->line;
    discharge_vars(c, e, line);
    switch (e->type) {
    case EXPR_NIL:
    case EXPR_FALSE:
        if (!cond) {
            return NO_JUMP;
        }
        break;
    case EXPR_TRUE:
    case EXPR_NUMBER:
    case EXPR_CONSTANT:
        if (cond) {
            return NO_JUMP;
        }
        break;
    default:
        break;
    }

    // lcode.c:jumponcond(FuncState *fs, expdesc *e, int cond)
    u16 rb = discharge_any_reg(c, e, line);
    pop_expr(c, e);
    // lcode.c:condjump(FuncState *fs, OpCode op, int A, int B, int C)
    compiler_code_abc(c, OP_TEST_SET, NO_REG, rb, cast(u16)!cond, line);
    return compiler_jump_new(c, e->line);
}

void
compiler_logical_new(Compiler *c, Expr *left, bool cond)
{
    isize jump_pc = get_logical_target(c, left, cond);

    if (cond) {
        lulu_assert(left->patch_false == NO_JUMP);
        compiler_jump_add(c, &left->patch_false, jump_pc);
        compiler_jump_patch(c, left->patch_true);
        left->patch_true = NO_JUMP;
    } else {
        lulu_assert(left->patch_true == NO_JUMP);
        compiler_jump_add(c, &left->patch_true, jump_pc);
        compiler_jump_patch(c, left->patch_false);
        left->patch_false = NO_JUMP;
    }
}

void
compiler_logical_patch(Compiler *c, Expr *left, Expr *right, bool cond)
{
    if (cond) {
        lulu_assert(left->patch_true == NO_JUMP);
        discharge_vars(c, right, right->line);
        compiler_jump_add(c, &right->patch_false, left->patch_false);
    } else {
        lulu_assert(left->patch_false == NO_JUMP);
        discharge_vars(c, right, right->line);
        compiler_jump_add(c, &right->patch_true, left->patch_true);
    }

    // Replace the contents of `left` with `right` so we know what to assign with.
    *left = *right;
}
