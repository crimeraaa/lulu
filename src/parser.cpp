#include <stdio.h>

#include "compiler.hpp"
#include "parser.hpp"
#include "vm.hpp"
#include "debug.hpp"

Parser
parser_make(lulu_VM &vm, String source, String script)
{
    Parser p{vm, lexer_make(vm, source, script), {}};
    return p;
}

// Forward declaration for recursive descent parsing.
static Expr
expression(Parser &p, Compiler &c, Precedence prec = PREC_NONE);

[[noreturn]]
static void
error_at(Parser &p, Token t, const char *msg)
{
    String where = (t.type == TOKEN_EOF) ? token_strings[t.type] : t.lexeme;
    vm_syntax_error(p.vm, p.lexer.source, t.line,
        "%s at '" STRING_FMTSPEC "'", msg, string_fmtarg(where));
}

[[noreturn]]
static void
error(Parser &p, const char *msg)
{
    error_at(p, p.consumed, msg);
}

static void
advance(Parser &p)
{
    p.consumed = lexer_lex(p.lexer);
}

static void
expect(Parser &p, Token_Type expected)
{
    if (p.consumed.type != expected) {
        char buf[32];
        sprintf(buf, "Expected '" STRING_FMTSPEC "'",
            string_fmtarg(token_strings[expected]));
        error(p, buf);
    }
}

static Expr
prefix_expr(Parser &p, Compiler &c)
{
    switch (p.consumed.type) {
    case TOKEN_NUMBER: {
        Expr e;
        e.type   = EXPR_NUMBER;
        e.line   = p.consumed.line;
        e.number = p.consumed.number;
        return e;
    }
    case TOKEN_OPEN_PAREN: {
        advance(p); // Skip '('.
        Expr e = expression(p, c);
        expect(p, TOKEN_CLOSE_PAREN);
        return e;
    }
    default:
        error(p, "Expected an expression");
    }
}

static void
arith(Parser &p, Compiler &c, Expr &left, Precedence left_prec, OpCode op, Precedence right_prec)
{
    if (left_prec < right_prec) {
        advance(p); // Skip the operator, point to first token of right
        compiler_expr_rk(c, left);
        Expr right = expression(p, c, right_prec);
        compiler_code_arith(c, op, left, right);
    }
}

static Precedence
infix_expr(Parser &p, Compiler &c, Expr &left, Precedence prec)
{
    switch (p.consumed.type) {
    case TOKEN_PLUS:     arith(p, c, left, prec, OP_ADD, PREC_TERMINAL); break;
    case TOKEN_DASH:     arith(p, c, left, prec, OP_SUB, PREC_TERMINAL); break;
    case TOKEN_ASTERISK: arith(p, c, left, prec, OP_MUL, PREC_FACTOR);   break;
    case TOKEN_SLASH:    arith(p, c, left, prec, OP_DIV, PREC_FACTOR);   break;
    case TOKEN_PERCENT:  arith(p, c, left, prec, OP_MOD, PREC_FACTOR);   break;
    case TOKEN_CARET:    arith(p, c, left, prec, OP_POW, PREC_EXPONENT); break;
    default:
        break;
    }
    return PREC_NONE;
}

/**
 * @note 2025-06-14:
 *  -   Assumes we just consumed the first (prefix) token.
 */
static Expr
expression(Parser &p, Compiler &c, Precedence prec)
{
    Expr e = prefix_expr(p, c);
    advance(p);
    infix_expr(p, c, e, prec);
    return e;
}

void
parser_program(lulu_VM &vm, Chunk &chunk, String script)
{
    Parser   p = parser_make(vm, chunk.source, script);
    Compiler c = compiler_make(vm, p, chunk);
    // Set up first token
    advance(p);
    Expr e = expression(p, c);
    compiler_expr_next_reg(c, e);
    expect(p, TOKEN_EOF);
    compiler_code(c, OP_RETURN, 0, 0, 0, p.lexer.line);
    debug_disassemble(c.chunk);
}
