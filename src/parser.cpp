#include <stdio.h>

#include "compiler.hpp"
#include "parser.hpp"
#include "vm.hpp"
#include "debug.hpp"

// Forward declaration for recursive descent parsing.
static Expr
expression(Parser &p, Compiler &c, Precedence limit = PREC_NONE);

Parser
parser_make(lulu_VM &vm, String source, String script)
{
    Parser p{vm, lexer_make(vm, source, script), {}};
    return p;
}

[[noreturn]]
static void
error_at(Parser &p, const Token &t, const char *msg)
{
    String where = (t.type == TOKEN_EOF) ? token_strings[t.type] : t.lexeme;
    vm_syntax_error(p.vm, p.lexer.source, t.line,
        "%s at '" STRING_FMTSPEC "'", msg, string_fmtarg(where));
}

void
parser_error(Parser &p, const char *msg)
{
    error_at(p, p.consumed, msg);
}

/**
 * @brief
 *  -   Move to the next token unconditionally.
 */
static void
advance(Parser &p)
{
    p.consumed = lexer_lex(p.lexer);
}

static bool
check(Parser &p, Token_Type expected)
{
    return p.consumed.type == expected;
}

static bool
match(Parser &p, Token_Type expected)
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
consume(Parser &p, Token_Type expected)
{
    if (!match(p, expected)) {
        // Assume our longest token is '<identifier>'.
        char buf[64];
        sprintf(buf, "Expected '" STRING_FMTSPEC "'",
            string_fmtarg(token_strings[expected]));
        parser_error(p, buf);
    }
}

static Expr
variable(Parser &p, Compiler &c, const Token &t)
{
    Expr e;
    e.type  = EXPR_GLOBAL;
    e.line  = t.line;
    e.index = compiler_add_constant(c, ostring_new(p.vm, t.lexeme));
    return e;
}

static Expr
prefix_expr(Parser &p, Compiler &c)
{
    Token t = p.consumed;
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
        e.index = compiler_add_constant(c, t.ostring);
        return e;
    }
    case TOKEN_IDENTIFIER: {
        return variable(p, c, t);
    }
    case TOKEN_OPEN_PAREN: {
        Expr e = expression(p, c);
        consume(p, TOKEN_CLOSE_PAREN);
        return e;
    }
    case TOKEN_DASH: {
        Expr e = expression(p, c, PREC_UNARY);
        compiler_code_unary(c, OP_UNM, e);
        return e;
    }
    case TOKEN_NOT: {
        Expr e = expression(p, c, PREC_UNARY);
        compiler_code_unary(c, OP_NOT, e);
        return e;
    }
    default:
        error_at(p, t, "Expected an expression");
    }
}

struct Binary {
    OpCode     op;
    Precedence left_prec;
    Precedence right_prec;
    bool       cond;
};

static constexpr Binary
left_associative(OpCode op, Precedence prec, bool cond = false)
{
    Binary b{op, prec, Precedence(cast_int(prec) + 1), cond};
    return b;
}

static constexpr Binary
right_associative(OpCode op, Precedence prec)
{
    Binary b{op, prec, prec, false};
    return b;
}

/**
 * @note 2025-06-16:
 *  -   `OP_RETURN` is our 'invalid' binary opcode.
 */
Binary get_binary(Token_Type type)
{
    switch (type) {
    case TOKEN_PLUS:       return left_associative(OP_ADD,  PREC_TERMINAL);
    case TOKEN_DASH:       return left_associative(OP_SUB,  PREC_TERMINAL);
    case TOKEN_ASTERISK:   return left_associative(OP_MUL,  PREC_FACTOR);
    case TOKEN_SLASH:      return left_associative(OP_DIV,  PREC_FACTOR);
    case TOKEN_PERCENT:    return left_associative(OP_MOD,  PREC_FACTOR);
    case TOKEN_CARET:      return right_associative(OP_POW, PREC_EXPONENT);
    case TOKEN_EQ:         return left_associative(OP_EQ,   PREC_EQUALITY,    true);
    case TOKEN_LESS:       return left_associative(OP_LT,   PREC_COMPARISON,  true);
    case TOKEN_LESS_EQ:    return left_associative(OP_LEQ,  PREC_COMPARISON,  true);
    // `x ~= y` <==> `not (x == y)`
    // `x >  y` <==> `not (x <= y)`
    // `x >= y` <==> `not (x <  y)`
    case TOKEN_NOT_EQ:     return left_associative(OP_EQ,   PREC_EQUALITY,    false);
    case TOKEN_GREATER:    return left_associative(OP_LEQ,  PREC_COMPARISON,  false);
    case TOKEN_GREATER_EQ: return left_associative(OP_LT,   PREC_COMPARISON,  false);
    case TOKEN_CONCAT:     return right_associative(OP_CONCAT, PREC_CONCAT);
    default:
        break;
    }
    return {OP_RETURN, PREC_NONE, PREC_NONE, false};
}


/**
 * @note 2025-06-14:
 *  -   Assumes we just consumed the first (prefix) token.
 */
static Expr
expression(Parser &p, Compiler &c, Precedence limit)
{
    Expr left = prefix_expr(p, c);
    for (;;) {
        Binary b = get_binary(p.consumed.type);
        if (b.op == OP_RETURN || limit > b.left_prec) {
            break;
        }

        // Skip operator, point to first token of right hand side argument.
        advance(p);

        switch (b.op) {
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_MOD:
        case OP_POW: {
            // VERY important to call this *before* parsing the right hand side,
            // if it ends up in a register we want them to be in the correct order.
            compiler_expr_rk(c, left);
            Expr right = expression(p, c, b.right_prec);
            compiler_code_arith(c, b.op, left, right);
            break;
        }
        case OP_EQ:
        case OP_LT:
        case OP_LEQ: {
            compiler_expr_rk(c, left);
            Expr right = expression(p, c, b.right_prec);
            compiler_code_compare(c, b.op, b.cond, left, right);
            break;
        }
        case OP_CONCAT: {
            // Don't put `left` in an RK register no matter what.
            compiler_expr_next_reg(c, left);
            Expr right = expression(p, c, b.right_prec);
            compiler_code_concat(c, left, right);
            break;
        }
        default:
            lulu_assertf(false, "Invalid binary opcode '%s'", opcode_names[b.op]);
            lulu_unreachable();
            break;
        }

    }
    return left;
}

/**
 * @brief
 *  -   Pushes a comma-separated list of expressions to the stack, except for
 *      the last one.
 *  -   We don't push the last one to allow optimizations.
 */
static Expr
expr_list(Parser &p, Compiler &c, u16 &n)
{
    n = 1;
    Expr e = expression(p, c);
    while (match(p, TOKEN_COMMA)) {
        compiler_expr_next_reg(c, e);
        e = expression(p, c);
        n++;
    }
    return e;
}

static void
return_stmt(Parser &p, Compiler &c, int line)
{
    u8   ra = u8(c.free_reg);
    u16  n;
    Expr e = expr_list(p, c, n);
    compiler_expr_next_reg(c, e);
    compiler_code(c, OP_RETURN, ra, u16(ra) + n, 0, line);
}

struct Assign {
    Assign *prev;
    Expr    variable;
};

static void
assignment(Parser &p, Compiler &c, Assign *last, u16 n_vars = 1)
{
    if (last->variable.type != EXPR_GLOBAL) {
        parser_error(p, "Expected an identifier");
    }

    if (match(p, TOKEN_COMMA)) {
        Assign next{last, prefix_expr(p, c)};
        assignment(p, c, &next, n_vars + 1);
        return;
    }

    consume(p, TOKEN_ASSIGN);

    u16 n_exprs;
    Expr e = expr_list(p, c, n_exprs);
    compiler_expr_next_reg(c, e);

    // Need to initialize remaining variables with `nil`.
    if (n_vars > n_exprs) {
        u16 reg   = c.free_reg;
        u16 n_nil = n_vars - n_exprs;
        compiler_reserve_reg(c, n_vars - n_exprs);
        compiler_load_nil(c, u8(reg), n_nil, e.line);
    } else {
        // Otherwise, just pop the extra expressions.
        c.free_reg -= (n_exprs - n_vars);
    }

    // Assign from rightmost target going leftmost. Use assigning expressions
    // from right to left as well.
    for (Assign *a = last; a != nullptr; a = a->prev) {
        Expr v = a->variable;
        switch (v.type) {
        case EXPR_GLOBAL:
            compiler_code(c, OP_SET_GLOBAL, u8(c.free_reg - 1), v.index, v.line);
            break;
        default:
            lulu_unreachable();
            break;
        }
        c.free_reg -= 1;
    }
}

static void
declaration(Parser &p, Compiler &c)
{
    Token t = p.consumed;
    switch (t.type) {
    case TOKEN_IDENTIFIER: {
        Assign a{nullptr, prefix_expr(p, c)};
        assignment(p, c, &a);
        break;
    }
    case TOKEN_RETURN:
        advance(p);
        return_stmt(p, c, t.line);
        break;
    default:
        error_at(p, t, "Expected an expression");
        break;
    }
    match(p, TOKEN_SEMI);
}

void
parser_program(lulu_VM &vm, Chunk &chunk, String script)
{
    Parser   p = parser_make(vm, chunk.source, script);
    Compiler c = compiler_make(vm, p, chunk);
    // Set up first token
    advance(p);
    while (!check(p, TOKEN_EOF)) {
        declaration(p, c);
    }
    consume(p, TOKEN_EOF);
    compiler_code(c, OP_RETURN, 0, 0, 0, p.lexer.line);
    debug_disassemble(c.chunk);
}
