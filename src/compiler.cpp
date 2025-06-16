#include "compiler.hpp"

Compiler
compiler_make(lulu_VM &vm, Parser &p, Chunk &chunk)
{
    Compiler c{vm, p, chunk, 0};
    return c;
}

//=== BYTECODE MANIPULATION ================================================ {{{

int
compiler_code(Compiler &c, OpCode op, u8 a, u16 b, u16 c2, int line)
{
    lulu_assert(opinfo_a(op) == OPARG_REG || opinfo_a(op) == OPARG_BOOL);
    lulu_assert((opinfo_b(op) & OPARG_REG_CONSTANT)
        || opinfo_b(op) == OPARG_ARGC);

    lulu_assert((opinfo_c(op) & OPARG_REG_CONSTANT)
        || opinfo_c(op) == OPARG_BOOL
        || opinfo_c(op) == OPARG_UNUSED);

    return chunk_append(c.vm, c.chunk, instruction_abc(op, a, b, c2), line);
}

int
compiler_code(Compiler &c, OpCode op, u8 a, u32 bx, int line)
{
    lulu_assert(opinfo_a(op) == OPARG_REG);
    lulu_assert(opinfo_b(op) == OPARG_CONSTANT);
    lulu_assert(opinfo_c(op) == OPARG_UNUSED);
    return chunk_append(c.vm, c.chunk, instruction_abx(op, a, bx), line);
}

void
compiler_load_nil(Compiler &c, u8 reg, int n, int line)
{
    size_t pc = len(c.chunk.code);
    // Stack frame is initialized to all `nil` at the start of the function, so
    // nothing to do.
    if (pc == 0) {
        return;
    }

    Instruction *ip = &c.chunk.code[pc - 1];
    u16 last_reg = cast(u16, reg) + cast(u16, n);
    // Previous instruction may be able to be folded?
    if (getarg_op(*ip) == OP_LOAD_NIL) {
        u16 ra = cast(u16, getarg_a(*ip));
        u16 rb = getarg_b(*ip);

        // New argument B could be used to update the previous?
        if (ra <= last_reg && last_reg > rb) {
            setarg_b(ip, last_reg);
            return;
        }
    }

    // Can't fold; need new instruction.
    Instruction i = instruction_abc(OP_LOAD_NIL, reg, last_reg, 0);
    chunk_append(c.vm, c.chunk, i, line);
}

void
compiler_load_boolean(Compiler &c, u8 reg, bool b, int line)
{
    Instruction i = instruction_abc(OP_LOAD_BOOL, reg, cast(u16, b), 0);
    chunk_append(c.vm, c.chunk, i, line);
}

u32
compiler_add_constant(Compiler &c, Value v)
{
    return chunk_add_constant(c.vm, c.chunk, v);
}

u32
compiler_add_constant(Compiler &c, Number n)
{
    return compiler_add_constant(c, value_make(n));
}

//=== }}} ======================================================================

//=== REGISTER MANIPULATION ================================================ {{{

void
compiler_reserve_reg(Compiler &c, u16 n)
{
    c.free_reg += n;
    // TODO(2025-06-16): Turn into handle-able error rather than unrecoverable
    lulu_assertf(c.free_reg <= MAX_REG, "More than %u registers", MAX_REG);
    if (c.chunk.stack_used < c.free_reg) {
        c.chunk.stack_used = c.free_reg;
    }
}

static void
pop_reg(Compiler &c, u8 reg)
{
    if (!reg_is_rk(reg)) {
        c.free_reg -= 1;
        // E.g. if we discharged 1 number, free_reg would be 1 and the expr.reg
        // would be 0. So when we pop that number from its register, we expect
        // to see free_reg == 0 again.
        lulu_assertf(c.free_reg == reg, "Register %u cannot be popped", reg);
    }
}

static void
pop_expr(Compiler &c, const Expr &e)
{
    if (e.type == EXPR_DISCHARGED) {
        pop_reg(c, e.reg);
    }
}

//=== }}} ======================================================================

static void
expr_to_reg(Compiler &c, Expr &e, u8 reg, int line)
{
    switch (e.type) {
    case EXPR_NIL:
        compiler_load_nil(c, reg, 1, line);
        break;
    case EXPR_TRUE: // fall-through
    case EXPR_FALSE:
        compiler_load_boolean(c, reg, e.type == EXPR_TRUE, line);
        break;
    case EXPR_NUMBER: {
        u32 i = compiler_add_constant(c, e.number);
        compiler_code(c, OP_CONSTANT, reg, i, line);
        break;
    }
    case EXPR_RELOCABLE: {
        Instruction *ip = &c.chunk.code[e.pc];
        setarg_a(ip, reg);
        break;
    }
    default:
        lulu_assertf(false, "Expr_Type(%i) not yet implemented", e.type);
        break;
    }
    e.reg  = reg;
    e.type = EXPR_DISCHARGED;
}

u8
compiler_expr_next_reg(Compiler &c, Expr &e)
{
    compiler_reserve_reg(c, 1);
    expr_to_reg(c, e, c.free_reg - 1, e.line);
    return c.free_reg - 1;
}

u8
compiler_expr_any_reg(Compiler &c, Expr &e)
{
    if (e.type == EXPR_DISCHARGED) {
        return e.reg;
    }
    return compiler_expr_next_reg(c, e);
}

static u16
value_to_rk(Compiler &c, Expr &e, Value v)
{
    u32 i   = compiler_add_constant(c, v);
    e.type  = EXPR_CONSTANT;
    e.index = i;

    // `i` can be encoded directly into an RK register?
    if (i <= cast(u32, OPCODE_MAX_RK)) {
        // Return the bit-toggled one index.
        return reg_to_rk(cast(u16, i));
    }
    // Can't fit in an RK register.
    return cast(u16, compiler_expr_any_reg(c, e));
}

u16
compiler_expr_rk(Compiler &c, Expr &e)
{
    switch (e.type) {
    case EXPR_NIL:    return value_to_rk(c, e, value_make());
    case EXPR_TRUE:   return value_to_rk(c, e, value_make(true));
    case EXPR_FALSE:  return value_to_rk(c, e, value_make(false));
    case EXPR_NUMBER: return value_to_rk(c, e, value_make(e.number));

    // May reach here if we previously called this.
    case EXPR_CONSTANT: {
        if (e.index <= cast(u32, OPCODE_MAX_RK)) {
            return reg_to_rk(cast(u16, e.index));
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
compiler_code_arith(Compiler &c, OpCode op, Expr &left, Expr &right)
{
    bool is_unary = (op == OP_UNM);

    // When unary minus, `right` is a dummy expression for the number 0.0;
    // don't push it because we need to do nothing when 'popping' it.
    u16 rkc = (is_unary) ? 0 : compiler_expr_rk(c, right);
    u16 rkb = compiler_expr_rk(c, left);

    if (rkc > rkb) {
        pop_expr(c, right);
        pop_expr(c, left);
    } else {
        pop_expr(c, left);
        pop_expr(c, right);
    }

    int pc = compiler_code(c, op, OPCODE_MAX_A, rkb, rkc, left.line);
    left.type = EXPR_RELOCABLE;
    left.pc   = pc;
}
