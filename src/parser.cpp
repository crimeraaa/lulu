#include <stdio.h>

#include "parser.hpp"
#include "compiler.hpp"
#include "debug.hpp"
#include "vm.hpp"

struct Expr_List {
    Expr last;
    u16  count;
};

// Forward declaration for recursive descent parsing.
static Expr
expression(Parser *p, Compiler *c, Precedence limit = PREC_NONE);

Parser
parser_make(lulu_VM *vm, LString source, LString script, Builder *b)
{
    Parser p{vm, lexer_make(vm, source, script, b), {}, b};
    return p;
}

[[noreturn]]
static void
error_at(Parser *p, const Token *t, const char *msg)
{
    LString where = (t->type == TOKEN_EOF) ? token_strings[t->type] : t->lexeme;

    // It is highly important we use a separate string builder from VM, because
    // we don't want it to conflict when writing the formatted string.
    builder_write_string(p->vm, p->builder, where);
    const char *s = builder_to_cstring(p->builder);
    vm_syntax_error(p->vm, p->lexer.source, t->line, "%s at '%s'", msg, s);
}

void
parser_error(Parser *p, const char *msg)
{
    error_at(p, &p->consumed, msg);
}

/**
 * @brief
 *  -   Move to the next token unconditionally.
 */
static void
advance(Parser *p)
{
    p->consumed = lexer_lex(&p->lexer);
}

static bool
check(Parser *p, Token_Type expected)
{
    return p->consumed.type == expected;
}

static bool
match(Parser *p, Token_Type expected)
{
    bool b = check(p, expected);
    if (b) {
        advance(p);
    }
    return b;
}


/**
 * @brief
 *  -   Asserts that the current token is of type `expected` and advances.
 */
static void
consume(Parser *p, Token_Type expected)
{
    if (!match(p, expected)) {
        // Assume our longest token is '<identifier>'.
        char buf[64];
        sprintf(buf, "Expected " STRING_QFMTSPEC,
            string_fmtarg(token_strings[expected]));
        parser_error(p, buf);
    }
}


/**
 * @brief
 *  -   Pushes a comma-separated list of expressions to the stack, except for
 *      the last one.
 *  -   We don't push the last one to allow optimizations.
 */
static Expr_List
expr_list(Parser *p, Compiler *c)
{
    Expr e = expression(p, c);
    u16  n = 1;
    while (match(p, TOKEN_COMMA)) {
        compiler_expr_next_reg(c, &e);
        e = expression(p, c);
        n++;
    }
    return {e, n};
}

static Expr
resolve_variable(Parser *p, Compiler *c, const Token *t)
{
    Expr e;
    e.type  = EXPR_GLOBAL;
    e.line  = t->line;
    e.index = compiler_add_ostring(c, ostring_new(p->vm, t->lexeme));
    return e;
}

static Expr
prefix_expr(Parser *p, Compiler *c)
{
    Token t = p->consumed;
    advance(p); // Skip '<number>', '<identifier>', '(' or '-'.
    switch (t.type) {
    case TOKEN_NIL: {
        Expr e{EXPR_NIL, t.line, {}};
        return e;
    }
    case TOKEN_TRUE: {
        Expr e{EXPR_TRUE, t.line, {}};
        return e;
    }
    case TOKEN_FALSE: {
        Expr e{EXPR_FALSE, t.line, {}};
        return e;
    }
    case TOKEN_NUMBER: {
        Expr e{EXPR_NUMBER, t.line, {t.number}};
        return e;
    }
    case TOKEN_STRING: {
        Expr e;
        e.type  = EXPR_CONSTANT;
        e.line  = t.line;
        e.index = compiler_add_ostring(c, t.ostring);
        return e;
    }
    case TOKEN_IDENTIFIER: {
        return resolve_variable(p, c, &t);
    }
    case TOKEN_OPEN_PAREN: {
        Expr e = expression(p, c);
        consume(p, TOKEN_CLOSE_PAREN);
        return e;
    }
    case TOKEN_DASH: {
        Expr e = expression(p, c, PREC_UNARY);
        compiler_code_unary(c, OP_UNM, &e);
        return e;
    }
    case TOKEN_NOT: {
        Expr e = expression(p, c, PREC_UNARY);
        compiler_code_unary(c, OP_NOT, &e);
        return e;
    }
    default:
        error_at(p, &t, "Expected an expression");
    }
}

/**
 * @note 2025-06-24
 *  Assumptions:
 *  1.) The caller `e` was pushed to a register.
 *  2.) Our current token is the one right after `(`.
 */
static void
function_call(Parser *p, Compiler *c, Expr *e)
{
    Expr_List args;
    if (!check(p, TOKEN_CLOSE_PAREN)) {
        args = expr_list(p, c);
        compiler_set_returns(c, &args.last, VARARG);
    } else {
        args.last.type = EXPR_NONE;
        args.count = 0;
    }
    consume(p, TOKEN_CLOSE_PAREN);

    lulu_assert(e->type == EXPR_DISCHARGED);
    u16 base = e->reg;
    if (args.last.type == EXPR_CALL) {
        args.count = VARARG;
    } else {
        // Close last argument.
        if (args.last.type != EXPR_NONE) {
            compiler_expr_next_reg(c, &args.last);
        }
        args.count = u16(c->free_reg - (base + 1));
    }
    e->type = EXPR_CALL;
    e->pc   = compiler_code_abc(c, OP_CALL, u8(base), args.count, 0, e->line);

    // By default, remove the arguments but not the function's register.
    // This allows use to 'reserve' the register.
    c->free_reg = base + 1;
}


static Expr
primary_expr(Parser *p, Compiler *c)
{
    Expr e = prefix_expr(p, c);
    for (;;) {
        switch (p->consumed.type) {
        case TOKEN_OPEN_PAREN: {
            // Function to be called must be on top of the stack.
            compiler_expr_next_reg(c, &e);
            advance(p);
            function_call(p, c, &e);
            break;
        }
        default:
            return e;
        }
    }
    return e;
}

struct Binary_Prec {
    Precedence left, right;
};

static constexpr Binary_Prec
left_assoc(Precedence prec)
{
    return {prec, Precedence(cast_int(prec) + 1)};
}

static constexpr Binary_Prec
right_assoc(Precedence prec)
{
    return {prec, prec};
}

static Binary_Prec
binary_precs[] = {
    /* BINARY_NONE */   left_assoc(PREC_NONE),
    /* BINARY_ADD */    left_assoc(PREC_TERMINAL),
    /* BINARY_SUB */    left_assoc(PREC_TERMINAL),
    /* BINARY_MUL */    left_assoc(PREC_FACTOR),
    /* BINARY_DIV */    left_assoc(PREC_FACTOR),
    /* BINARY_MOD */    left_assoc(PREC_FACTOR),
    /* BINARY_POW */    right_assoc(PREC_EXPONENT),
    /* BINARY_EQ */     left_assoc(PREC_COMPARISON),
    /* BINARY_LT */     left_assoc(PREC_COMPARISON),
    /* BINARY_LEQ */    left_assoc(PREC_COMPARISON),
    /* BINARY_NEQ */    left_assoc(PREC_COMPARISON),
    /* BINARY_GEQ */    left_assoc(PREC_COMPARISON),
    /* BINARY_GT */     left_assoc(PREC_COMPARISON),
    /* BINARY_CONCAT */ right_assoc(PREC_CONCAT),
};

static OpCode
binary_opcodes[] = {
    /* BINARY_NONE */   OP_RETURN,
    /* BINARY_ADD */    OP_ADD,
    /* BINARY_SUB */    OP_SUB,
    /* BINARY_MUL */    OP_MUL,
    /* BINARY_DIV */    OP_DIV,
    /* BINARY_MOD */    OP_MOD,
    /* BINARY_POW */    OP_POW,
    /* BINARY_EQ */     OP_EQ,
    /* BINARY_LT */     OP_LT,
    /* BINARY_LEQ */    OP_LEQ,
    /* BINARY_NEQ */    OP_EQ,
    /* BINARY_GEQ */    OP_LEQ,
    /* BINARY_GT */     OP_LT,
    /* BINARY_CONCAT */ OP_CONCAT,
};

/**
 * @note 2025-06-16:
 *  -   `OP_RETURN` is our 'invalid' binary opcode.
 */
static Binary_Type
get_binary(Token_Type type)
{
    switch (type) {
    case TOKEN_PLUS:       return BINARY_ADD;
    case TOKEN_DASH:       return BINARY_SUB;
    case TOKEN_ASTERISK:   return BINARY_MUL;
    case TOKEN_SLASH:      return BINARY_DIV;
    case TOKEN_PERCENT:    return BINARY_MOD;
    case TOKEN_CARET:      return BINARY_POW;
    case TOKEN_EQ:         return BINARY_EQ;
    case TOKEN_NOT_EQ:     return BINARY_NEQ;
    case TOKEN_LESS:       return BINARY_LT;
    case TOKEN_LESS_EQ:    return BINARY_LEQ;
    case TOKEN_GREATER:    return BINARY_GT;
    case TOKEN_GREATER_EQ: return BINARY_GEQ;
    case TOKEN_CONCAT:     return BINARY_CONCAT;
    default:
        break;
    }
    return BINARY_NONE;
}


/**
 * @note 2025-06-14:
 *  -   Assumes we just consumed the first (prefix) token.
 */
static Expr
expression(Parser *p, Compiler *c, Precedence limit)
{
    Expr left = primary_expr(p, c);
    for (;;) {
        Binary_Type b = get_binary(p->consumed.type);
        if (b == BINARY_NONE || limit > binary_precs[b].left) {
            break;
        }

        // Skip operator, point to first token of right hand side argument->
        advance(p);

        bool cond = true;
        switch (b) {
        case BINARY_ADD:
        case BINARY_SUB:
        case BINARY_MUL:
        case BINARY_DIV:
        case BINARY_MOD:
        case BINARY_POW: {
            // VERY important to call this *before* parsing the right hand side,
            // if it ends up in a register we want them to be in the correct order.
            compiler_expr_rk(c, &left);
            Expr right = expression(p, c, binary_precs[b].right);
            compiler_code_arith(c, binary_opcodes[b], &left, &right);
            break;
        }
        case BINARY_NEQ: // fall-through
        case BINARY_GT:  // fall-through
        case BINARY_GEQ: cond = false;
        case BINARY_EQ:  // fall-through
        case BINARY_LT:  // fall-through
        case BINARY_LEQ: {
            compiler_expr_rk(c, &left);
            Expr right = expression(p, c, binary_precs[b].right);
            compiler_code_compare(c, binary_opcodes[b], cond, &left, &right);
            break;
        }
        case BINARY_CONCAT: {
            // Don't put `left` in an RK register no matter what.
            compiler_expr_next_reg(c, &left);
            Expr right = expression(p, c, binary_precs[b].right);
            compiler_code_concat(c, &left, &right);
            break;
        }
        default:
            lulu_assertf(false, "Invalid Binary_Type(%i)", b);
            lulu_unreachable();
            break;
        }

    }
    return left;
}

static void
return_stmt(Parser *p, Compiler *c, int line)
{
    const u8  ra = u8(c->free_reg);
    Expr_List e  = expr_list(p, c);
    compiler_expr_next_reg(c, &e.last);
    compiler_code_return(c, ra, e.count, /* is_vararg */ false, line);
}

struct Assign {
    Assign *prev;
    Expr    variable;
};

static void
adjust_assign(Compiler *c, u16 n_vars, Expr_List *e)
{
    int extra = cast_int(n_vars) - cast_int(e->count);
    // The last assigning expression can have variadic returns.
    if (e->last.type == EXPR_CALL) {
        // Include the call itself.
        extra++;
        if (extra < 0) {
            extra = 0;
        }
        compiler_set_returns(c, &e->last, u16(extra));
        if (extra > 1) {
            compiler_reserve_reg(c, u16(extra - 1));
        }
    } else {
        // Need to close last expression?
        if (e->last.type != EXPR_NONE) {
            compiler_expr_next_reg(c, &e->last);
        }
        if (extra > 0) {
            // Register of first uninitialized right-hand side.
            u16 reg = c->free_reg;
            compiler_reserve_reg(c, u16(extra));
            compiler_load_nil(c, u8(reg), extra, e->last.line);
        }
    }
}

static void
assignment(Parser *p, Compiler *c, Assign *last, u16 n_vars = 1)
{
    // Check the result of `expression()`.
    if (last->variable.type != EXPR_GLOBAL) {
        parser_error(p, "Expected an identifier");
    }

    if (match(p, TOKEN_COMMA)) {
        Assign next{last, expression(p, c)};
        assignment(p, c, &next, n_vars + 1);
        return;
    }

    consume(p, TOKEN_ASSIGN);

    Expr_List e    = expr_list(p, c);
    Assign   *iter = last;
    if (n_vars != e.count) {
        adjust_assign(c, n_vars, &e);
        // Reuse the registers occupied by the extra values.
        if (e.count > n_vars) {
            c->free_reg -= u16(e.count - n_vars);
        }
    } else {
        compiler_set_one_return(c, &e.last);
        compiler_set_variable(c, &last->variable, &e.last);
        iter = iter->prev;
    }

    // Assign from rightmost target going leftmost. Use assigning expressions
    // from right to left as well.
    while (iter != nullptr) {
        Expr tmp;
        tmp.type = EXPR_DISCHARGED;
        tmp.line = iter->variable.line; // Probably correct
        tmp.reg  = u8(c->free_reg - 1);
        compiler_set_variable(c, &iter->variable, &tmp);
        iter = iter->prev;
    }
}


static void
declaration(Parser *p, Compiler *c)
{
    Token t = p->consumed;
    switch (t.type) {
    case TOKEN_IDENTIFIER: {
        Assign a{nullptr, expression(p, c)};
        // Differentiate `f().field = ...` and `f()`.
        if (a.variable.type == EXPR_CALL) {
            compiler_set_returns(c, &a.variable, 0);
            c->free_reg -= 1;
        } else {
            assignment(p, c, &a);
        }
        break;
    }
    case TOKEN_RETURN:
        advance(p);
        return_stmt(p, c, t.line);
        break;
    default:
        error_at(p, &t, "Expected an expression");
        break;
    }
    match(p, TOKEN_SEMI);
}

Chunk *
parser_program(lulu_VM *vm, LString source, LString script, Builder *b)
{
    Table *t  = table_new(vm);
    Chunk *ch = chunk_new(vm, source, t);

    // Push chunk and table to stack so that they are not collected while we
    // are executing.
    vm_push(vm, Value(ch));
    vm_push(vm, Value(t));

    Parser   p = parser_make(vm, source, script, b);
    Compiler c = compiler_make(vm, &p, ch);
    // Set up first token
    advance(&p);
    while (!check(&p, TOKEN_EOF)) {
        declaration(&p, &c);
    }
    consume(&p, TOKEN_EOF);
    compiler_code_abc(&c, OP_RETURN, 0, 0, 0, p.lexer.line);

#ifdef LULU_DEBUG_PRINT_CODE
    debug_disassemble(c.chunk);
#endif // LULU_DEBUG_PRINT_CODE

    vm_pop(vm);
    vm_pop(vm);

    return ch;
}
