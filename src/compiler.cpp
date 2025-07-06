#include "compiler.hpp"
#include "vm.hpp"

Compiler
compiler_make(lulu_VM *vm, Parser *p, Chunk *chunk)
{
    Compiler c;
    c.vm       = vm;
    c.parser   = p;
    c.chunk    = chunk;
    c.free_reg = 0;
    small_array_init(&c.active);
    return c;
}

//=== BYTECODE MANIPULATION ================================================ {{{

isize
compiler_code_abc(Compiler *c, OpCode op, u16 a, u16 b, u16 c2, int line)
{
    lulu_assert(opinfo_a(op) == OPARG_REGK || opinfo_a(op) == OPARG_OTHER);
    lulu_assert(opinfo_b(op) == OPARG_REGK || opinfo_b(op) == OPARG_OTHER);
    lulu_assert(opinfo_c(op) == OPARG_REGK
        || opinfo_c(op) == OPARG_OTHER
        || opinfo_c(op) == OPARG_UNUSED);

    return chunk_append(c->vm, c->chunk, instruction_abc(op, a, b, c2), line);
}

isize
compiler_code_abx(Compiler *c, OpCode op, u16 a, u32 bx, int line)
{
    lulu_assert(opinfo_a(op) == OPARG_REGK);
    lulu_assert(opinfo_b(op) == OPARG_REGK || opinfo_b(op) == OPARG_OTHER);
    lulu_assert(opinfo_c(op) == OPARG_UNUSED || opinfo_c(op) == OPARG_OTHER);
    return chunk_append(c->vm, c->chunk, instruction_abx(op, a, bx), line);
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

    Instruction *ip = &c->chunk->code[pc - 1];
    u16 last_reg = reg + cast(u16)(n - 1);
    // Previous instruction may be able to be folded?
    if (getarg_op(*ip) == OP_LOAD_NIL) {
        u16 ra = getarg_a(*ip);
        u16 rb = getarg_b(*ip);

        // New argument B could be used to update the previous?
        if (ra <= last_reg && last_reg > rb) {
            setarg_b(ip, last_reg);
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
    return compiler_add_value(c, value_make_number(n));
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
    if (c->free_reg > MAX_REG) {
        parser_error(c->parser, "Too many registers");
    }
    if (c->chunk->stack_used < c->free_reg) {
        c->chunk->stack_used = c->free_reg;
    }
}

static void
pop_reg(Compiler *c, u16 reg)
{
    // `reg` is not a constant index nor a local register?
    if (!reg_is_rk(reg) && reg >= cast(u16)small_array_len(c->active)) {
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

//=== }}} ======================================================================

static void
discharge_vars(Compiler *c, Expr *e)
{
    switch (e->type) {
    case EXPR_GLOBAL:
        e->type = EXPR_RELOCABLE;
        e->pc   = compiler_code_abx(c, OP_GET_GLOBAL, OPCODE_MAX_A, e->index, e->line);
        break;
    case EXPR_LOCAL:
        // locals are already in registers
        e->type = EXPR_DISCHARGED;
        break;
    case EXPR_INDEXED: {
        u16 t = e->table.reg;
        u16 k = e->table.field_rk;
        e->type = EXPR_RELOCABLE;
        e->pc   = compiler_code_abc(c, OP_GET_TABLE, OPCODE_MAX_A, t, k, e->line);
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
expr_to_reg(Compiler *c, Expr *e, u16 reg, int line)
{
    discharge_vars(c, e);
    switch (e->type) {
    case EXPR_NIL:
        compiler_load_nil(c, reg, 1, line);
        break;
    case EXPR_TRUE: // fall-through
    case EXPR_FALSE:
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
        Instruction *ip = &c->chunk->code[e->pc];
        setarg_a(ip, reg);
        break;
    }
    case EXPR_DISCHARGED: {
        if (e->reg != reg) {
            compiler_code_abc(c, OP_MOVE, reg, e->reg, 0, line);
        }
        break;
    }
    default:
        lulu_assertf(false, "Expr_Type(%i) not yet implemented", e->type);
        lulu_unreachable();
        return;
    }
    e->reg  = reg;
    e->type = EXPR_DISCHARGED;
}

u16
compiler_expr_next_reg(Compiler *c, Expr *e)
{
    discharge_vars(c, e);
    pop_expr(c, e);

    u16 reg = c->free_reg;
    compiler_reserve_reg(c, 1);
    expr_to_reg(c, e, reg, e->line);
    return reg;
}

u16
compiler_expr_any_reg(Compiler *c, Expr *e)
{
    if (e->type == EXPR_DISCHARGED) {
        return e->reg;
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
    if (i <= OPCODE_MAX_RK) {
        // Return the bit-toggled index.
        return reg_to_rk(i);
    }
    // Can't fit in an RK register.
    return compiler_expr_any_reg(c, e);
}

u16
compiler_expr_rk(Compiler *c, Expr *e)
{
    switch (e->type) {
    case EXPR_NIL:    return value_to_rk(c, e, nil);
    case EXPR_TRUE:   return value_to_rk(c, e, value_make_boolean(true));
    case EXPR_FALSE:  return value_to_rk(c, e, value_make_boolean(false));
    case EXPR_NUMBER: return value_to_rk(c, e, value_make_number(e->number));

    // May reach here if we previously called this.
    case EXPR_CONSTANT: {
        if (e->index <= OPCODE_MAX_RK) {
            return reg_to_rk(cast(u16)e->index);
        }
        break;
    }
    default:
        break;
    }
    // If already discharged, don't push it again.
    return compiler_expr_any_reg(c, e);
}

void
compiler_code_arith(Compiler *c, OpCode op, Expr *left, Expr *right)
{
    lulu_assert((OP_ADD <= op && op <= OP_POW) || op == OP_CONCAT);
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
    left->pc   = compiler_code_abc(c, op, OPCODE_MAX_A, rkb, rkc, left->line);
}

void
compiler_code_unary(Compiler *c, OpCode op, Expr *e)
{
    lulu_assert(OP_UNM <= op && op <= OP_NOT);

    // Unary minus and unary `not` cannot operate on RK registers.
    u16 rb = compiler_expr_next_reg(c, e);
    pop_expr(c, e);

    e->type = EXPR_RELOCABLE;
    e->pc   = compiler_code_abc(c, op, OPCODE_MAX_A, rb, 0, e->line);
}

static void
swap(u16 *a, u16 *b)
{
    u16 tmp = *a;
    *a = *b;
    *b = tmp;
}

void
compiler_code_compare(Compiler *c, OpCode op, bool cond, Expr *left, Expr *right)
{
    lulu_assert(OP_EQ <= op && op <= OP_LEQ);

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
    left->pc   = compiler_code_abc(c, op, OPCODE_MAX_A, rkb, rkc, left->line);

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
        Instruction *ip = &c->chunk->code[right->pc];
        if (getarg_op(*ip) == OP_CONCAT) {
            lulu_assert(left->reg == getarg_b(*ip) - 1);
            pop_expr(c, left);
            setarg_b(ip, left->reg);
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
        u16 reg = compiler_expr_any_reg(c, expr);
        compiler_code_abc(c, OP_MOVE, var->reg, reg, 0, var->line);
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
        Instruction *ip = &c->chunk->code[call->pc];
        setarg_c(ip, n);
    }
}

void
compiler_set_one_return(Compiler *c, Expr *e)
{
    if (e->type == EXPR_CALL) {
        Instruction *ip = &c->chunk->code[e->pc];
        setarg_c(ip, 1);
        e->type = EXPR_DISCHARGED;
        e->reg  = getarg_a(*ip);
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
    *reg = 0;
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
