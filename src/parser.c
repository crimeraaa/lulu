#include "parser.h"
#include "chunk.h"
#include "object.h"

// Forward declarations to allow recursive descent parsing.
static ParseRule *get_parserule(TkType key);
static void parse_precedence(Compiler *self, Precedence prec);

// Intern a variable name.
// static int parse_variable(Compiler *self, const char *info) {
//     Lexer *lexer = self->lexer;
//     consume_token(lexer, TK_IDENT, info);
//     return identifier_constant(self, &lexer->consumed);
// }

/**
 * @note    In the book, Robert uses `parsePrecedence(PREC_ASSIGNMENT)` but
 *          doing that will allow nested assignments as in C. For Lua, we have
 *          to use 1 precedence higher to disallow them.
 */
static void expression(Compiler *self);
static void statement(Compiler *self);

// INFIX EXPRESSIONS ------------------------------------------------------ {{{1

static OpCode get_binop(TkType optype) {
    switch (optype) {
    case TK_PLUS:    return OP_ADD;
    case TK_DASH:    return OP_SUB;
    case TK_STAR:    return OP_MUL;
    case TK_SLASH:   return OP_DIV;
    case TK_PERCENT: return OP_MOD;
    case TK_CARET:   return OP_POW;

    case TK_EQ:      return OP_EQ;
    case TK_LT:      return OP_LT;
    case TK_LE:      return OP_LE;
    default:
        // Should not happen
        return OP_RETURN;
    }
}

// Assumes we just consumed a binary operator as a possible infix expression,
// and that the left-hand side has been fully compiled.
static void binary(Compiler *self) {
    Lexer *lexer  = self->lexer;
    Token *token  = &lexer->consumed;
    TkType optype = token->type;
    ParseRule *rule = get_parserule(optype);

    // For exponentiation enforce right-associativity.
    parse_precedence(self, rule->prec + (optype != TK_CARET));

    // NEQ, GT and GE must be encoded as logical NOT of their counterparts.
    switch (optype) {
    case TK_NEQ: emit_nbytes(self, OP_EQ, OP_NOT);    break;
    case TK_GT:  emit_nbytes(self, OP_LE, OP_NOT);    break;
    case TK_GE:  emit_nbytes(self, OP_LT, OP_NOT);    break;
    default:     emit_byte(self,  get_binop(optype)); break;
    }
}

// Assumes we just consumed a `..` and the first argument has been compiled.
static void concat(Compiler *self) {
    Lexer *lexer = self->lexer;
    int argc = 1;

    do {
        // Although right associative, we don't recursively compile concat
        // expressions in the same grouping.
        parse_precedence(self, PREC_CONCAT + 1);
        argc++;
    } while (match_token(lexer, TK_CONCAT));

    emit_byte(self, OP_CONCAT);
    emit_byte3(self, argc);
}

// 1}}} ------------------------------------------------------------------------

// PREFIX EXPRESSIONS ----------------------------------------------------- {{{1

static void literal(Compiler *self) {
    Lexer *lexer  = self->lexer;
    Token *token  = &lexer->consumed;
    TkType optype = token->type;
    switch (optype) {
    case TK_NIL:    emit_byte(self, OP_NIL);   break;
    case TK_TRUE:   emit_byte(self, OP_TRUE);  break;
    case TK_FALSE:  emit_byte(self, OP_FALSE); break;
    default:
        // Should not happen
        break;
    }
}

// Assumes we just consumed a '(' as a possible prefix expression: a grouping.
static void grouping(Compiler *self) {
    Lexer *lexer = self->lexer;
    expression(self);
    consume_token(lexer, TK_RPAREN, "Expected ')' after expression");
}

// Assumes we just consumed a possible number literal as a prefix expression.
static void number(Compiler *self) {
    Lexer *lexer = self->lexer;
    Token *token = &lexer->consumed;
    char *endptr; // Populated by below function call
    Number value = cstr_tonumber(token->start, &endptr);

    // If this is true, strtod failed to convert the entire token/lexeme.
    if (endptr != (token->start + token->len)) {
        lexerror_at_consumed(lexer, "Malformed number");
    } else {
        emit_constant(self, &make_number(value));
    }
}

static void string(Compiler *self) {
    Lexer *lexer = self->lexer;
    Token *token = &lexer->consumed;

    // Left +1 to skip left quote, len -2 to get offset of last non-quote.
    TString *interned = copy_string(self->vm, token->start + 1, token->len - 2);
    emit_constant(self, &make_string(interned));
}

// `assignable` is only true if the identifier is also the first statement.
static void named_variable(Compiler *self, const Token *name, bool assignable) {
    Lexer *lexer = self->lexer;
    int arg = identifier_constant(self, name);
    if (assignable) {
        if (match_token(lexer, TK_ASSIGN)) {
            expression(self);
            define_variable(self, arg);
        } else {
            lexerror_at_token(lexer, "'=' expected");
        }
    } else {
        emit_byte(self, OP_GETGLOBAL);
        emit_byte3(self, arg);
    }
}

// Past the first lexeme, assigning of variables is not allowed in Lua.
static void variable(Compiler *self) {
    Lexer *lexer = self->lexer;
    named_variable(self, &lexer->consumed, false);
}

static OpCode get_unop(TkType type) {
    switch (type) {
    case TK_NOT:    return OP_NOT;
    case TK_DASH:   return OP_UNM;
    case TK_POUND:  return OP_LEN;
    default:
        // Should not happen
        return OP_RETURN;
    }
}

// Assumes a leading operator has been consumed as prefix expression, e.g. '-'.
static void unary(Compiler *self) {
    Lexer *lexer  = self->lexer;
    Token *token  = &lexer->consumed;
    TkType optype = token->type; // Save due to recursion when compiling.

    // Recursively compiles until we hit something with a lower precedence.
    parse_precedence(self, PREC_UNARY);
    emit_byte(self, get_unop(optype));
}

// 1}}} ------------------------------------------------------------------------

// PARSING PRECEDENCE ----------------------------------------------------- {{{1

static ParseRule PARSERULES_LOOKUP[] = {
    // TOKEN           PREFIXFN     INFIXFN     PRECEDENCE
    [TK_AND]        = {NULL,        NULL,       PREC_AND},
    [TK_BREAK]      = {NULL,        NULL,       PREC_NONE},
    [TK_DO]         = {NULL,        NULL,       PREC_NONE},
    [TK_ELSE]       = {NULL,        NULL,       PREC_NONE},
    [TK_ELSEIF]     = {NULL,        NULL,       PREC_NONE},
    [TK_END]        = {NULL,        NULL,       PREC_NONE},
    [TK_FALSE]      = {literal,     NULL,       PREC_NONE},
    [TK_FOR]        = {NULL,        NULL,       PREC_NONE},
    [TK_FUNCTION]   = {NULL,        NULL,       PREC_NONE},
    [TK_IF]         = {NULL,        NULL,       PREC_NONE},
    [TK_IN]         = {NULL,        NULL,       PREC_NONE},
    [TK_LOCAL]      = {NULL,        NULL,       PREC_NONE},
    [TK_NIL]        = {literal,     NULL,       PREC_NONE},
    [TK_NOT]        = {unary,       NULL,       PREC_NONE},
    [TK_OR]         = {NULL,        NULL,       PREC_NONE},
    [TK_PRINT]      = {NULL,        NULL,       PREC_NONE},
    [TK_RETURN]     = {NULL,        NULL,       PREC_NONE},
    [TK_THEN]       = {NULL,        NULL,       PREC_NONE},
    [TK_TRUE]       = {literal,     NULL,       PREC_NONE},
    [TK_WHILE]      = {NULL,        NULL,       PREC_NONE},

    [TK_LPAREN]     = {grouping,    NULL,       PREC_NONE},
    [TK_RPAREN]     = {NULL,        NULL,       PREC_NONE},
    [TK_LBRACKET]   = {NULL,        NULL,       PREC_NONE},
    [TK_RBRACKET]   = {NULL,        NULL,       PREC_NONE},
    [TK_LCURLY]     = {NULL,        NULL,       PREC_NONE},
    [TK_RCURLY]     = {NULL,        NULL,       PREC_NONE},

    [TK_COMMA]      = {NULL,        NULL,       PREC_NONE},
    [TK_SEMICOL]    = {NULL,        NULL,       PREC_NONE},
    [TK_VARARG]     = {NULL,        NULL,       PREC_NONE},
    [TK_CONCAT]     = {NULL,        concat,     PREC_CONCAT},
    [TK_PERIOD]     = {NULL,        NULL,       PREC_NONE},
    [TK_POUND]      = {unary,       NULL,       PREC_UNARY},

    [TK_PLUS]       = {NULL,        binary,     PREC_TERMINAL},
    [TK_DASH]       = {unary,       binary,     PREC_TERMINAL},
    [TK_STAR]       = {NULL,        binary,     PREC_FACTOR},
    [TK_SLASH]      = {NULL,        binary,     PREC_FACTOR},
    [TK_PERCENT]    = {NULL,        binary,     PREC_FACTOR},
    [TK_CARET]      = {NULL,        binary,     PREC_POW},

    [TK_ASSIGN]     = {NULL,        NULL,       PREC_NONE},
    [TK_EQ]         = {NULL,        binary,     PREC_EQUALITY},
    [TK_NEQ]        = {NULL,        binary,     PREC_EQUALITY},
    [TK_GT]         = {NULL,        binary,     PREC_COMPARISON},
    [TK_GE]         = {NULL,        binary,     PREC_COMPARISON},
    [TK_LT]         = {NULL,        binary,     PREC_COMPARISON},
    [TK_LE]         = {NULL,        binary,     PREC_COMPARISON},

    [TK_IDENT]      = {variable,    NULL,       PREC_NONE},
    [TK_STRING]     = {string,      NULL,       PREC_NONE},
    [TK_NUMBER]     = {number,      NULL,       PREC_NONE},
    [TK_ERROR]      = {NULL,        NULL,       PREC_NONE},
    [TK_EOF]        = {NULL,        NULL,       PREC_NONE},
};

static void expression(Compiler *self) {
    parse_precedence(self, PREC_ASSIGN + 1);
}

// static void vardecl(Compiler *self) {
//     Lexer *lexer = self->lexer;
//     int index = parse_variable(self, "Expected a variable name");
//     // Lua does not allow implicit nil for globals
//     consume_token(lexer, TK_ASSIGN, "Expected '=' after global variable name");
//     expression(self);
//     define_variable(self, index);
// }

static void print_statement(Compiler *self) {
    Lexer *lexer = self->lexer;
    expression(self);
    match_token(lexer, TK_SEMICOL);
    emit_byte(self, OP_PRINT);
}

void declaration(Compiler *self) {
    Lexer *lexer = self->lexer;
    /* In Lua, globals aren't declared, but rather assigned as needed. This may
    be inconsistent with my design that accessing undefined globals is an error,
    but at the same time I dislike implicit nil for undefined globals. */
    if (match_token(lexer, TK_IDENT)) {
        named_variable(self, &lexer->consumed, true);
    } else {
        statement(self);
    }
}

// Expressions produce values, but since statements need to have 0 stack effect
// this will pop whatever it produces.
static void exprstmt(Compiler *self) {
    expression(self);
    emit_byte(self, OP_POP);
    emit_byte3(self, 1);
}

// By themselves, statements have zero stack effect.
static void statement(Compiler *self) {
    Lexer *lexer = self->lexer;
    if (match_token(lexer, TK_PRINT)) {
        print_statement(self);
    } else {
        exprstmt(self);
    }
    // Lua allows 1 semicolon to terminate statements, but no more.
    match_token(lexer, TK_SEMICOL);
}

// Assumes the first token is ALWAYS a prefix expression with 0 or more infix
// exprssions following it.
static void parse_precedence(Compiler *self, Precedence prec) {
    Lexer *lexer = self->lexer;
    next_token(lexer);
    ParseFn prefixfn = get_parserule(lexer->consumed.type)->prefixfn;
    if (prefixfn == NULL) {
        lexerror_at_consumed(lexer, "Expected a prefix expression");
        return;
    }
    prefixfn(self);

    // Is the token to our right something we can further compile?
    while (prec <= get_parserule(lexer->token.type)->prec) {
        next_token(lexer);
        ParseFn infixfn = get_parserule(lexer->consumed.type)->infixfn;
        infixfn(self);
    }

    // This function can never consume the `=` token.
    if (match_token(lexer, TK_ASSIGN)) {
        lexerror_at_token(lexer, "Invalid assignment target");
    }
}

static ParseRule *get_parserule(TkType key) {
    return &PARSERULES_LOOKUP[key];
}

// 1}}} ------------------------------------------------------------------------
