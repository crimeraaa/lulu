#include <stdio.h>  // sprintf
#include <stdlib.h> // abs

#include "compiler.hpp"
#include "vm.hpp"

static int
jump_get(Compiler *c, int jump_pc);

static void
jump_set(Compiler *c, int jump_pc, int target_pc);

static void
jump_invert(Compiler *c, Expr *e);

static Instruction *
jump_get_control(Compiler *c, int jump_pc);

static int
jump_if(Compiler *c, OpCode op, u16 a, u16 b, u16 c2);

void
compiler_error_limit(Compiler *c, int limit, const char *what)
{
    Parser     *p   = c->parser;
    const char *who = (c->prev == nullptr) ? "script" : "function";
    char        buf[128];
    sprintf(buf, "%s uses more than %i %s", who, limit, what);
    lexer_error(&p->lexer, p->current.type, buf, p->last_line);
}

//=== BYTECODE MANIPULATION ============================================ {{{

static int
code_push(Compiler *c, Instruction i)
{
    lulu_VM *L = c->L;
    Chunk   *p  = c->chunk;
    int line = c->parser->last_line;
    int pc = chunk_code_push(L, p, i, &c->pc);
    chunk_line_push(L, p, pc, line, &c->n_lines);
    return pc;
}

static void
code_pop(Compiler *c)
{
    c->pc--;
}

int
compiler_code_abc(Compiler *c, OpCode op, u16 a, u16 b, u16 c2)
{
    lulu_assert(opinfo[op].fmt() == OPFORMAT_ABC);
    lulu_assert(opinfo[op].b() == OPARG_REGK
        || opinfo[op].b() == OPARG_OTHER
        || b == 0);
    lulu_assert(opinfo[op].c() == OPARG_REGK
        || opinfo[op].c() == OPARG_OTHER
        || c2 == 0);
    return code_push(c, Instruction::make_abc(op, a, b, c2));
}

int
compiler_code_abx(Compiler *c, OpCode op, u16 a, u32 bx)
{
    lulu_assert(opinfo[op].fmt() == OPFORMAT_ABX);
    lulu_assert(opinfo[op].b() == OPARG_REGK || opinfo[op].b() == OPARG_OTHER);
    lulu_assert(opinfo[op].c() == OPARG_UNUSED);
    return code_push(c, Instruction::make_abx(op, a, bx));
}

int
compiler_code_asbx(Compiler *c, OpCode op, u16 a, i32 sbx)
{
    lulu_assert(opinfo[op].fmt() == OPFORMAT_ASBX);
    lulu_assert(opinfo[op].c() == OPARG_UNUSED);
    return code_push(c, Instruction::make_asbx(op, a, sbx));
}

void
compiler_load_nil(Compiler *c, u16 reg, int n)
{
    Instruction *ip       = nullptr;
    u16          last_reg = reg + static_cast<u16>(n - 1);

    // No potential jumps up to this point?
    int pc = c->pc;
    if (pc > c->last_target) {
        // Stack frame is initialized to all `nil` at the start of the function,
        // so nothing to do.
        if (pc == 0) {
            // Target register is a new local?
            if (static_cast<isize>(reg) >= small_array_len(c->active)) {
                return;
            }
            // Target register is an existing local.
            // Concept check: `local x; x = nil`
        }
        goto no_fold;
    }

    ip = get_code(c, pc - 1);
    // Previous instruction may be able to be folded?
    if (ip->op() == OP_NIL) {
        // New argument B could be used to update the previous?
        if (ip->a() <= last_reg && last_reg > ip->b()) {
            ip->set_b(last_reg);
            return;
        }
    }

// Can't fold; need new instruction.
no_fold:
    compiler_code_abc(c, OP_NIL, reg, last_reg, 0);
}

void
compiler_load_boolean(Compiler *c, u16 reg, bool b)
{
    compiler_code_abc(c, OP_BOOL, reg, static_cast<u16>(b), /*c=*/0);
}

static u32
add_constant(Compiler *c, Value k, Value v)
{
    bool key_exists;
    const Value i = table_get(c->indexes, k, &key_exists);
    // Constant found and is not a string literal mapping to true?
    if (key_exists && i.is_integer()) {
        return static_cast<u32>(i.to_integer());
    }

    lulu_VM *L = c->L;
    // Push value to prevent collection in case garbage collector runs in any
    // of the below calls.
    vm_push_value(L, v);
    u32 n = chunk_constant_push(L, c->chunk, v);
    Value *t_k = table_set(L, c->indexes, k);
    t_k->set_integer(static_cast<Integer>(n));
    vm_pop_value(L);
    return n;
}

u32
compiler_add_number(Compiler *c, Number n)
{
    Value v = Value::make_number(n);
    return add_constant(c, v, v);
}

u32
compiler_add_ostring(Compiler *c, OString *s)
{
    Value v = s->to_value();
    return add_constant(c, v, v);
}

//=== }}} ==================================================================

//=== REGISTER MANIPULATION ============================================ {{{

void
compiler_check_stack(Compiler *c, u16 n)
{
    u16 new_stack = c->free_reg + n;
    if (new_stack > c->chunk->stack_used) {
        compiler_check_limit(c, new_stack, MAX_REG, "registers");
        c->chunk->stack_used = new_stack;
    }
}

void
compiler_reserve_reg(Compiler *c, u16 n)
{
    compiler_check_stack(c, n);
    c->free_reg += n;
}

static void
pop_reg(Compiler *c, u16 reg)
{
    // `reg` is not a constant index nor a local register?
    if (!Instruction::reg_is_k(reg)
        && reg >= static_cast<u16>(small_array_len(c->active)))
    {
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

/** @brief Emits bytecode needed for variable retrieval (global, local,
 *  upvalue), or for table field retrieval.
 *
 * @note 2025-07-16
 *      In such case `e` is transformed to `EXPR_RELOCABLE` but its
 *      destination register is not yet set.
 */
static void
discharge_vars(Compiler *c, Expr *e)
{
    switch (e->type) {
    case EXPR_GLOBAL:
        e->type = EXPR_RELOCABLE;
        e->pc   = compiler_code_abx(c, OP_GET_GLOBAL, NO_REG, e->index);
        break;
    case EXPR_LOCAL:
        // locals are already in registers
        e->type = EXPR_DISCHARGED;
        break;
    case EXPR_INDEXED: {
        u16 t   = e->table.reg;
        u16 k   = e->table.field_rk;
        e->type = EXPR_RELOCABLE;
        e->pc   = compiler_code_abc(c, OP_GET_TABLE, NO_REG, t, k);
        // We can reuse these registers as they're no longer needed (for now).
        pop_reg(c, k);
        pop_reg(c, t);
        break;
    }
    case EXPR_UPVALUE: {
        e->type = EXPR_RELOCABLE;
        e->pc   = compiler_code_abc(c, OP_GET_UPVALUE, NO_REG, e->upvalue, 0);
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
discharge_to_reg(Compiler *c, Expr *e, u16 reg)
{
    discharge_vars(c, e);
    switch (e->type) {
    case EXPR_NIL:
        compiler_load_nil(c, reg, 1);
        break;
    case EXPR_FALSE:
    case EXPR_TRUE:
        compiler_load_boolean(c, reg, e->type == EXPR_TRUE);
        break;
    case EXPR_NUMBER: {
        u32 i = compiler_add_number(c, e->number);
        compiler_code_abx(c, OP_CONSTANT, reg, i);
        break;
    }
    case EXPR_CONSTANT: {
        compiler_code_abx(c, OP_CONSTANT, reg, e->index);
        break;
    }
    case EXPR_RELOCABLE: {
        Instruction *ip = get_code(c, e->pc);
        ip->set_a(reg);
        break;
    }
    case EXPR_DISCHARGED: {
        if (e->reg != reg) {
            compiler_code_abc(c, OP_MOVE, reg, e->reg, 0);
        }
        break;
    }
    default:
        lulu_assertf(e->type == EXPR_NONE || e->type == EXPR_JUMP,
            "Expr_Type(%i) cannot be discharged", e->type);
        return;
    }
    e->type = EXPR_DISCHARGED;
    e->reg  = reg;
}

static u16
discharge_any_reg(Compiler *c, Expr *e)
{
    if (e->type == EXPR_DISCHARGED) {
        return e->reg;
    }
    u16 reg = c->free_reg;
    compiler_reserve_reg(c, 1);
    discharge_to_reg(c, e, reg);
    return reg;
}

// see `lcode.c:exp2reg(FuncState *fs, expdesc *e, int reg)` in Lua 5.1.5.
static bool
need_value(Compiler *c, int jump_pc)
{
    while (jump_pc != NO_JUMP) {
        const int    next_pc = jump_get(c, jump_pc);
        Instruction *ctrl_ip = jump_get_control(c, jump_pc);
        // `OP_TEST_SET` already uses R(A) for its value; other opcodes do not
        // have a destination register yet.
        if (ctrl_ip->op() != OP_TEST_SET) {
            return true;
        }
        jump_pc = next_pc;
    }
    return false;
}

static int
label_code(Compiler *c, u16 reg, bool b, bool do_jump)
{
    compiler_label_get(c);
    return compiler_code_abc(c, OP_BOOL, reg, static_cast<u16>(b),
        static_cast<u16>(do_jump));
}

static void
expr_to_reg(Compiler *c, Expr *e, u16 reg)
{
    bool is_jump = (e->type == EXPR_JUMP);
    discharge_to_reg(c, e, reg);
    // comparison instruction itself is part of the truthy patch list.
    if (is_jump) {
        compiler_jump_add(c, &e->patch_true, e->pc);
    }
    if (e->has_jumps()) {
        int load_true  = NO_JUMP;
        int load_false = NO_JUMP;
        if (need_value(c, e->patch_true) || need_value(c, e->patch_false)) {
            int jump_pc = (is_jump) ? NO_JUMP : compiler_jump_new(c);
            load_false  = label_code(c, reg, /*b=*/false, /*do_jump=*/true);
            load_true   = label_code(c, reg, /*b=*/true,  /*do_jump=*/false);
            compiler_jump_patch(c, jump_pc);
        }
        compiler_jump_patch(c, e->patch_false, load_false, reg);
        compiler_jump_patch(c, e->patch_true, load_true, reg);
    }

    // If any jumps were present, they were discharged above. We can safely
    // reset them.
    *e = Expr::make_reg(EXPR_DISCHARGED, reg);
}

u16
compiler_expr_next_reg(Compiler *c, Expr *e)
{
    discharge_vars(c, e);
    pop_expr(c, e);

    u16 reg = c->free_reg;
    compiler_reserve_reg(c, 1);
    expr_to_reg(c, e, reg);
    return reg;
}

u16
compiler_expr_any_reg(Compiler *c, Expr *e)
{
    discharge_vars(c, e);
    if (e->type == EXPR_DISCHARGED) {
        if (!e->has_jumps()) {
            return e->reg;
        }
        lulu_panicln("Expr with jumps not yet implemented");
    }
    return compiler_expr_next_reg(c, e);
}


/**
 * @param v
 *      Must be a reference so we can compare its address to `nil`.
 */
static u16
value_to_rk(Compiler *c, Expr *e, const Value &v)
{
    // Pointer comparisons are faster than dedicated functions calls, and
    // the compiler will sort out the location for `constexpr inline` anyway.
    Value k  = (&v == &nil) ? c->indexes->to_value() : v;
    u32   i  = add_constant(c, k, v);
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
    case EXPR_FALSE:  return value_to_rk(c, e, Value::make_boolean(true));
    case EXPR_TRUE:   return value_to_rk(c, e, Value::make_boolean(false));
    case EXPR_NUMBER: return value_to_rk(c, e, Value::make_number(e->number));

    // May reach here if we previously called this.
    case EXPR_CONSTANT:
        if (e->index <= Instruction::MAX_RK) {
            return Instruction::reg_to_rk(static_cast<u16>(e->index));
        }
        break;
    default:
        break;
    }
    // If already discharged, don't push it again.
    return compiler_expr_any_reg(c, e);
}

//=== }}} ==================================================================

static bool
arith_folded(OpCode op, Expr *restrict left, const Expr *restrict right)
{
    // Cannot fold if at least one argument is not a number literal.
    if (!left->is_number() || !right->is_number()) {
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
    case OP_POW:
        n = lulu_Number_pow(a, b);
        break;
    default:
        lulu_unreachable();
        return false;
    }
    left->number = n;
    return true;
}

void
compiler_code_arith(Compiler *c, OpCode op, Expr *restrict left,
    Expr *restrict right)
{
    lulu_assert((OP_ADD <= op && op <= OP_POW) || op == OP_CONCAT);
    if (arith_folded(op, left, right)) {
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

    // Don't use `Expr::make_pc` as we might still have patch lists to
    // discharge.
    left->type = EXPR_RELOCABLE;
    left->pc   = compiler_code_abc(c, op, NO_REG, rkb, rkc);
}

void
compiler_code_unary(Compiler *c, OpCode op, Expr *e)
{
    lulu_assert(OP_UNM <= op && op <= OP_LEN);

    switch (op) {
    case OP_UNM:
        // Constant folding
        if (e->is_number()) {
            e->number = lulu_Number_unm(e->number);
            return;
        }
        break;
    case OP_NOT:
        // Constant folding?
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
        case EXPR_RELOCABLE: {
            Instruction *ip  = get_code(c, e->pc);
            OpCode       op2 = ip->op();
            if (OP_EQ <= op2 && op2 <= OP_LEQ) {
                bool cond = ip->a();
                ip->set_a(static_cast<u16>(!cond));
                return;
            }
            break;
        }
        case EXPR_JUMP:
            // Only occurs for comparisons, `OP_{EQ,LT,LEQ}`.
            jump_invert(c, e);
            return;
        default:
            break;
        }
        break;
    case OP_LEN:
        // Cannot possibly fold no matter what.
        break;
    default:
        lulu_unreachable();
        break;
    }

    // Unary minus and unary `not` cannot operate on RK registers.
    // We can, however, reuse existing registers.
    u16 rb = compiler_expr_any_reg(c, e);
    pop_expr(c, e);

    // Don't call `Expr::make_pc()` because we might still have patch lists
    // to discharge.
    e->type = EXPR_RELOCABLE;
    e->pc   = compiler_code_abc(c, op, NO_REG, rb, 0);
}

static void
expr_bool(Expr *e, bool b)
{
    e->type = (b) ? EXPR_TRUE : EXPR_FALSE;
}

static bool
compare_folded(OpCode op, bool cond, Expr *restrict left, Expr *restrict right)
{
    bool result;
    // For our sanity, only fold comparisons of number literals.
    // Folding 'true == false' and such are not worth optimizing for.
    if (!left->is_number() || !right->is_number()) {
        return false;
    }

    if (op == OP_EQ) {
        result = lulu_Number_eq(left->number, right->number);
        if (!cond) {
            result = !result;
        }
    } else {
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
compiler_code_compare(Compiler *c, OpCode op, bool cond, Expr *restrict left,
    Expr *restrict right)
{
    lulu_assert(OP_EQ <= op && op <= OP_LEQ);
    if (compare_folded(op, cond, left, right)) {
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
        cond = !cond;
    }

    left->type = EXPR_JUMP;
    left->pc   = jump_if(c, op, static_cast<u16>(cond), rkb, rkc);
}

void
compiler_code_concat(Compiler *c, Expr *restrict left, Expr *restrict right)
{
    /**
     * @details 2025-06-22
     *      Checks if we are able to fold consecutive concats. Folding is
     *      impossible with left-associavity due to argument pushing.
     *
     *      If `a..b..c` were parsed as `(a..b)..c`, then `(a..b)` would
     *      push `a` and `b` emit `OP_CONCAT`. By the time we reach `c`,
     *      there is nothing we can do to fold because the bytecode is
     *      already set.
     *
     *      `a..(b..c)` is better because we would have pushed `a`, `b` and
     *      `c` by the time we emit `OP_CONCAT`.
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
compiler_set_variable(Compiler *c, Expr *restrict var, Expr *restrict expr)
{
    switch (var->type) {
    case EXPR_GLOBAL: {
        u16 reg = compiler_expr_any_reg(c, expr);
        compiler_code_abx(c, OP_SET_GLOBAL, reg, var->index);
        break;
    }
    case EXPR_LOCAL: {
        // Pop if temporary register.
        pop_expr(c, expr);

        // Set destination register or code `OP_MOVE`.
        expr_to_reg(c, expr, var->reg);
        break;
    }
    case EXPR_INDEXED: {
        u16 t = var->table.reg;
        u16 k = var->table.field_rk;
        u16 v = compiler_expr_rk(c, expr);
        compiler_code_abc(c, OP_SET_TABLE, t, k, v);
        break;
    }
    case EXPR_UPVALUE: {
        u16 reg = compiler_expr_any_reg(c, expr);
        compiler_code_abc(c, OP_SET_UPVALUE, reg, var->upvalue, 0);
        break;
    }
    default:
        lulu_panicf("Non-assignable Expr_Type(%i)", var->type);
        break;
    }
    pop_expr(c, expr);
}

void
compiler_code_return(Compiler *c, u16 reg, u16 count)
{
    compiler_code_abc(c, OP_RETURN, reg, count, 0);
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

u16
compiler_get_local(Compiler *c, u16 limit, OString *ident)
{
    for (isize reg = small_array_len(c->active) - 1;
         reg >= static_cast<isize>(limit);
         reg--)
    {
        u16    i     = small_array_get(c->active, reg);
        Local *local = &c->chunk->locals[i];
        if (local->ident == ident) {
            return static_cast<u16>(reg);
        }
    }
    return NO_REG;
}

void
compiler_get_table(Compiler *c, Expr *restrict t, Expr *restrict k)
{
    u16 rkb           = compiler_expr_rk(c, k);
    t->type           = EXPR_INDEXED;
    t->table.reg      = t->reg;
    t->table.field_rk = rkb;
}

void
compiler_set_array(Compiler *c, u16 table_reg, isize n_array, isize to_store)
{
    lulu_assume(n_array > 0);

    /** @brief Subtract 1 so that `n_array == 50` still uses offset 0.
     *
     * @note(2025-08-04)
     *      Lua 5.1.5 actually adds 1 at the end because offset 0 is
     *      reserved to indicate the size is too big and the next
     *      instruction is to be interpreted as the size itself.
     */
    isize offset = (n_array - 1) / FIELDS_PER_FLUSH;
    isize b      = to_store;

    lulu_assert(to_store != 0);

    // Ensure array index offset would fit as a u16.
    lulu_assert(offset <= static_cast<isize>(Instruction::MAX_C));
    compiler_code_abc(c, OP_SET_ARRAY, table_reg, static_cast<u16>(b),
        static_cast<u16>(offset));

    // Pop all array elements.
    c->free_reg = table_reg + 1;
}

int
compiler_jump_new(Compiler *c)
{
    return compiler_code_asbx(c, OP_JUMP, 0, NO_JUMP);
}

void
compiler_jump_add(Compiler *c, int *list_pc, int jump_pc)
{
    // Nothing to do?
    if (jump_pc == NO_JUMP) {
        return;
    }
    // No list yet?
    else if (*list_pc == NO_JUMP)
    {
        *list_pc = jump_pc;
        return;
    }

    // `*pc` points to the start of the jump list, so get the last jump we
    // added so we can chain `jump_pc` into it.
    int pc = *list_pc;
    for (;;) {
        int next = jump_get(c, pc);
        if (next == NO_JUMP) {
            jump_set(c, pc, jump_pc);
            break;
        }
        pc = next;
    }
}

// Get the `pc` of the relative target in `jump_pc`, or `NO_JUMP`.
static int
jump_get(Compiler *c, int jump_pc)
{
    Instruction i = *get_code(c, jump_pc);
    lulu_assertf(
        OP_JUMP <= i.op() && i.op() <= OP_FOR_LOOP,
        "Got opcode '%s'",
        opnames[i.op()]
    );
    i32 offset = i.sbx();
    if (offset == NO_JUMP) {
        return NO_JUMP;
    }
    return (jump_pc + 1) + offset;
}

static void
jump_set(Compiler *c, int jump_pc, int target_pc)
{
    Instruction *ip = get_code(c, jump_pc);
    lulu_assert(OP_JUMP <= ip->op() && ip->op() <= OP_FOR_LOOP);

    int offset = target_pc - (jump_pc + 1);

    // Allow: `while true do end`
    // lulu_assert(offset != NO_JUMP);

    compiler_check_limit(c, abs(offset),
        static_cast<int>(Instruction::MAX_SBX), "jump offset");

    ip->set_sbx(static_cast<i32>(offset));
}

static void
jump_invert(Compiler *c, Expr *e)
{
    Instruction *ip = jump_get_control(c, e->pc);
    lulu_assert(opinfo[ip->op()].test());
    // Must be a comparison in order to flip argument A.
    lulu_assert(ip->op() != OP_TEST_SET && ip->op() != OP_TEST);

    bool cond = static_cast<bool>(ip->a());
    ip->set_a(static_cast<u16>(!cond));
}

static bool
test_reg_patch(Compiler *c, int jump_pc, u16 reg)
{
    Instruction *ip = jump_get_control(c, jump_pc);
    if (ip->op() != OP_TEST_SET) {
        return false;
    }

    u16 rb = ip->b();
    if (reg != NO_REG && reg != rb) {
        ip->set_a(reg);
    } else {
        // Destination register isn't needed or `rb` is already the destination.
        *ip = Instruction::make_abc(OP_TEST, rb, 0, ip->c());
    }
    return true;
}

static Instruction *
jump_get_control(Compiler *c, int jump_pc)
{
    Instruction *ip = get_code(c, jump_pc);
    lulu_assert(OP_JUMP <= ip->op() && ip->op() <= OP_FOR_LOOP);
    if (jump_pc >= 1) {
        Instruction *ctrl_ip = ip - 1;
        if (opinfo[ctrl_ip->op()].test()) {
            return ctrl_ip;
        }
    }
    return ip;
}

void
compiler_jump_patch(Compiler *c, int jump_pc, int target, u16 reg)
{
    if (jump_pc == NO_JUMP) {
        return;
    }

    if (target == NO_JUMP) {
        // Do not subtract 1; we want to skip over the latest instruction.
        target = compiler_label_get(c);
    }
    for (;;) {
        // Save because `jump_set()` will overwrite the original value.
        int next = jump_get(c, jump_pc);
        test_reg_patch(c, jump_pc, reg);
        jump_set(c, jump_pc, target);
        if (next == NO_JUMP) {
            break;
        }

        jump_pc = next;
    }
}

static int
logical_target_get(Compiler *c, Expr *left, bool cond)
{
    discharge_vars(c, left);
    switch (left->type) {
    case EXPR_NIL:
    case EXPR_FALSE:
        // left-hand side of `and` is always falsy?
        if (!cond) {
            return NO_JUMP;
        }
        break;
    case EXPR_TRUE:
    case EXPR_NUMBER:
    case EXPR_CONSTANT:
        // left-hand side of `or` is always truthy?
        if (cond) {
            return NO_JUMP;
        }
        break;
    case EXPR_JUMP:
        if (cond) {
            jump_invert(c, left);
        }
        return left->pc;
    default:
        break;
    }

    /**
     * @note
     *      See `lcode.c:jumponcond(FuncState *fs, expdesc *e, int cond)` in
     *      Lua 5.1.5.
     *
     * @note 2025-07-19
     *      In `lcode.c:luaK_goiftrue()`, `jumponcond()` is called with
     *      parameter `cond == 0`. So to simulate that, we don't negate
     *      `cond` when folding `OP_NOT` and we do negatie it when emitting
     *      `OP_TEST_SET`.
     */
    if (left->type == EXPR_RELOCABLE) {
        Instruction ip = *get_code(c, left->pc);
        if (ip.op() == OP_NOT) {
            // Remove previous `OP_NOT`, replace it with a jump-test pair.
            code_pop(c);
            return jump_if(c, OP_TEST, ip.b(), 0, static_cast<u16>(cond));
        }
    }

    u16 rb = discharge_any_reg(c, left);
    pop_expr(c, left);
    return jump_if(c, OP_TEST_SET, NO_REG, rb, static_cast<u16>(!cond));
}


/** @brief Emits an `OP_TEST{SET}`-`OP_JUMP` pair.
 *
 * @note(2025-07-18)
 *      Analogous to `lcode.c:condjump(FuncState *fs, OpCode op, int A,
 *      int B, int C)` in Lua 5.1.5.
 */
static int
jump_if(Compiler *c, OpCode op, u16 a, u16 b, u16 c2)
{
    compiler_code_abc(c, op, a, b, c2);
    return compiler_jump_new(c);
}

void
compiler_logical_new(Compiler *c, Expr *left, bool cond)
{
    int jump_pc = logical_target_get(c, left, cond);
    if (cond) {
        // logical-and
        // Falsy patch list may be non-empty.
        // Concept check: local x,y; if x and y then end
        compiler_jump_add(c, &left->patch_false, jump_pc);

        // Discharge previous jump, if any.
        compiler_jump_patch(c, left->patch_true);
        left->patch_true = NO_JUMP;
    } else {
        // logical or
        compiler_jump_add(c, &left->patch_true, jump_pc);

        // Discharge previous jump, if any.
        compiler_jump_patch(c, left->patch_false);
        left->patch_false = NO_JUMP;
    }
}

void
compiler_logical_patch(Compiler *c, Expr *restrict left, Expr *restrict right,
    bool cond)
{
    discharge_vars(c, right);

    /**
     * @brief
     *      Copy whatever patch list is in `left` to `right`.
     *      This is so we can discharge it later if we have multiple
     *      logicals in a row, e.g. `x and y or z`.
     *
     * @note 2025-07-18
     *      We cannot assume `expr_has_jumps(left)` because the condition
     *      may be folded and thus the patch list is set to `NO_JUMP`.
     */
    if (cond) {
        // logical and
        lulu_assert(left->patch_true == NO_JUMP);
        compiler_jump_add(c, &right->patch_false, left->patch_false);
    } else {
        // logical or
        lulu_assert(left->patch_false == NO_JUMP);
        compiler_jump_add(c, &right->patch_true, left->patch_true);
    }

    // Replace the contents of `left` with `right` so we know what to
    // assign with.
    *left = *right;
}
