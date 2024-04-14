#include "parser.h"
#include "chunk.h"

// Forward declarations to allow recursive descent parsing.
static const ParseRule *get_parserule(TkType key);
static void parse_precedence(Compiler *self, Precedence prec);

// INFIX EXPRESSIONS ------------------------------------------------------ {{{1

static OpCode get_binop(TkType optype) {
    switch (optype) {
    case TK_PLUS:    return OP_ADD;
    case TK_DASH:    return OP_SUB;
    case TK_STAR:    return OP_MUL;
    case TK_SLASH:   return OP_DIV;
    case TK_PERCENT: return OP_MOD;
    case TK_CARET:   return OP_POW;
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
    const ParseRule *rule = get_parserule(optype);

    // For exponentiation, enforce right-associativity.
    parse_precedence(self, (optype == TK_CARET) ? rule->prec : rule->prec + 1);
    emit_byte(self, get_binop(optype));
}

// 1}}} ------------------------------------------------------------------------

// PREFIX EXPRESSIONS ----------------------------------------------------- {{{1

static void literal(Compiler *self) {
    switch (self->lexer->consumed.type) {
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

static OpCode get_unop(TkType type) {
    switch (type) {
    case TK_NOT:    return OP_NOT;
    case TK_DASH:   return OP_UNM;
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

static const ParseRule parserules[] = {
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
    [TK_CONCAT]     = {NULL,        NULL,       PREC_NONE},
    [TK_PERIOD]     = {NULL,        NULL,       PREC_NONE},

    [TK_PLUS]       = {NULL,        binary,     PREC_TERMINAL},
    [TK_DASH]       = {unary,       binary,     PREC_TERMINAL},
    [TK_STAR]       = {NULL,        binary,     PREC_FACTOR},
    [TK_SLASH]      = {NULL,        binary,     PREC_FACTOR},
    [TK_PERCENT]    = {NULL,        binary,     PREC_FACTOR},
    [TK_CARET]      = {NULL,        binary,     PREC_FACTOR},

    [TK_ASSIGN]     = {NULL,        NULL,       PREC_NONE},
    [TK_EQ]         = {NULL,        NULL,       PREC_NONE},
    [TK_NEQ]        = {NULL,        NULL,       PREC_NONE},
    [TK_GT]         = {NULL,        NULL,       PREC_NONE},
    [TK_GE]         = {NULL,        NULL,       PREC_NONE},
    [TK_LT]         = {NULL,        NULL,       PREC_NONE},
    [TK_LE]         = {NULL,        NULL,       PREC_NONE},

    [TK_IDENT]      = {NULL,        NULL,       PREC_NONE},
    [TK_STRING]     = {NULL,        NULL,       PREC_NONE},
    [TK_NUMBER]     = {number,      NULL,       PREC_NONE},
    [TK_ERROR]      = {NULL,        NULL,       PREC_NONE},
    [TK_EOF]        = {NULL,        NULL,       PREC_NONE},
};

void expression(Compiler *self) {
    parse_precedence(self, PREC_ASSIGN + 1);
}

// Assumes the first token is ALWAYS a prefix expression with 0 or more infix
// exprssions following it.
static void parse_precedence(Compiler *self, Precedence prec) {
    Lexer *lexer    = self->lexer;
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
}

static const ParseRule *get_parserule(TkType key) {
    return &parserules[key];
}

// 1}}} ------------------------------------------------------------------------
