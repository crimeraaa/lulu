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
expression(Parser &p, Compiler &c, Precedence limit = PREC_NONE);

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
    case TOKEN_DASH: {
        advance(p);
        Expr e = expression(p, c, PREC_UNARY);
        // unary minus cannot operate on RK registers.
        compiler_expr_next_reg(c, e);
        compiler_code_arith(c, OP_UNM, e, e);
        return e;
    }
    default:
        error(p, "Expected an expression");
    }
}

struct Binary {
    OpCode     op;
    Precedence left_prec;
    Precedence right_prec;
};

static constexpr Binary
left_associative(OpCode op, Precedence prec)
{
    Binary b{op, prec, cast(Precedence, cast_int(prec) + 1)};
    return b;
}

static constexpr Binary
right_associative(OpCode op, Precedence prec)
{
    Binary b{op, prec, prec};
    return b;
}


Binary get_binop(Token_Type type)
{
    switch (type) {
    case TOKEN_PLUS:     return left_associative(OP_ADD, PREC_TERMINAL);
    case TOKEN_DASH:     return left_associative(OP_SUB, PREC_TERMINAL);
    case TOKEN_ASTERISK: return left_associative(OP_MUL, PREC_FACTOR);
    case TOKEN_SLASH:    return left_associative(OP_DIV, PREC_FACTOR);
    case TOKEN_PERCENT:  return left_associative(OP_MOD, PREC_FACTOR);
    case TOKEN_CARET:    return right_associative(OP_POW, PREC_EXPONENT);
    default:
        break;
    }
    return {OP_RETURN, PREC_NONE, PREC_NONE};
}


/**
 * @note 2025-06-14:
 *  -   Assumes we just consumed the first (prefix) token.
 */
static Expr
expression(Parser &p, Compiler &c, Precedence limit)
{
    Expr left = prefix_expr(p, c);
    advance(p);
    for (;;) {
        Binary b = get_binop(p.consumed.type);
        if (b.op == OP_RETURN || b.left_prec < limit) {
            break;
        }

        // Skip operator, point to first token of right hand side argument.
        advance(p);

        // VERY important to call this *before* parsing the right hand side, if
        // it ends up in a register we want them to be in the correct order.
        compiler_expr_rk(c, left);
        Expr right = expression(p, c, b.right_prec);
        compiler_code_arith(c, b.op, left, right);
    }
    return left;
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
