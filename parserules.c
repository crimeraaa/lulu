#include "parserules.h"

extern void grouping(Compiler *self);
extern void binary(Compiler *self);
extern void unary(Compiler *self);
extern void number(Compiler *self);
extern void literal(Compiler *self);

/**
 * III:17.6     A Pratt Parser
 * 
 * This thing is huge! It mirrors the `TokenType` enum.
 * 
 * NOTE:
 * 
 * Because we need to access functions that are not defined here and are also not
 * declared in `compiler.h`, we NEED them to be non-static in `compiler.c`.
 * 
 * This is so we can link to them properly. If they were static, the linker would
 * not be able to resolve the symbols here.
 */
static const ParseRule rules[TOKEN_COUNT] = {
    // Single character tokens
    [TOKEN_LPAREN]          = {grouping,    NULL,       PREC_NONE},
    [TOKEN_RPAREN]          = {NULL,        NULL,       PREC_NONE},
    [TOKEN_LBRACE]          = {NULL,        NULL,       PREC_NONE},
    [TOKEN_RBRACE]          = {NULL,        NULL,       PREC_NONE},
    [TOKEN_LBRACKET]        = {NULL,        NULL,       PREC_NONE},
    [TOKEN_RBRACKET]        = {NULL,        NULL,       PREC_NONE},
    [TOKEN_COMMA]           = {NULL,        NULL,       PREC_NONE},
    [TOKEN_PERIOD]          = {NULL,        NULL,       PREC_NONE},
    [TOKEN_COLON]           = {NULL,        NULL,       PREC_NONE},
    [TOKEN_POUND]           = {NULL,        NULL,       PREC_NONE},
    [TOKEN_SEMICOL]         = {NULL,        NULL,       PREC_NONE},
    [TOKEN_ASSIGN]          = {NULL,        NULL,       PREC_NONE},
    
    // Arithmetic Operators
    [TOKEN_PLUS]            = {NULL,        binary,     PREC_TERMINAL},
    [TOKEN_DASH]            = {unary,       binary,     PREC_TERMINAL},
    [TOKEN_STAR]            = {NULL,        binary,     PREC_FACTOR},
    [TOKEN_SLASH]           = {NULL,        binary,     PREC_FACTOR},
    [TOKEN_CARET]           = {NULL,        binary,     PREC_EXPONENT},
    [TOKEN_PERCENT]         = {NULL,        binary,     PREC_FACTOR},
    
    // Relational Operators
    [TOKEN_EQ]              = {NULL,        binary,     PREC_EQUALITY},
    [TOKEN_NEQ]             = {NULL,        binary,     PREC_EQUALITY},
    [TOKEN_GT]              = {NULL,        binary,     PREC_COMPARISON},
    [TOKEN_GE]              = {NULL,        binary,     PREC_COMPARISON},
    [TOKEN_LT]              = {NULL,        binary,     PREC_COMPARISON},
    [TOKEN_LE]              = {NULL,        binary,     PREC_COMPARISON},

    // Literals
    [TOKEN_FALSE]           = {literal,     NULL,       PREC_NONE},
    [TOKEN_IDENT]           = {NULL,        NULL,       PREC_NONE},
    [TOKEN_NIL]             = {literal,     NULL,       PREC_NONE},
    [TOKEN_NUMBER]          = {number,      NULL,       PREC_NONE},
    [TOKEN_STRING]          = {NULL,        NULL,       PREC_NONE},
    [TOKEN_TABLE]           = {NULL,        NULL,       PREC_NONE},
    [TOKEN_TRUE]            = {literal,     NULL,       PREC_NONE},

    // Keywords
    [TOKEN_AND]             = {NULL,        NULL,       PREC_NONE},
    [TOKEN_BREAK]           = {NULL,        NULL,       PREC_NONE},
    [TOKEN_DO]              = {NULL,        NULL,       PREC_NONE},
    [TOKEN_ELSE]            = {NULL,        NULL,       PREC_NONE},
    [TOKEN_ELSEIF]          = {NULL,        NULL,       PREC_NONE},
    [TOKEN_END]             = {NULL,        NULL,       PREC_NONE},
    [TOKEN_FOR]             = {NULL,        NULL,       PREC_NONE},
    [TOKEN_FUNCTION]        = {NULL,        NULL,       PREC_NONE},
    [TOKEN_IF]              = {NULL,        NULL,       PREC_NONE},
    [TOKEN_IN]              = {NULL,        NULL,       PREC_NONE},
    [TOKEN_LOCAL]           = {NULL,        NULL,       PREC_NONE},
    [TOKEN_NOT]             = {unary,       NULL,       PREC_NONE},
    [TOKEN_OR]              = {NULL,        NULL,       PREC_NONE},
    [TOKEN_RETURN]          = {NULL,        NULL,       PREC_NONE},
    [TOKEN_SELF]            = {NULL,        NULL,       PREC_NONE},
    [TOKEN_THEN]            = {NULL,        NULL,       PREC_NONE},
    [TOKEN_WHILE]           = {NULL,        NULL,       PREC_NONE},

    // Misc.
    [TOKEN_CONCAT]          = {NULL,        NULL,       PREC_NONE},
    [TOKEN_VARARGS]         = {NULL,        NULL,       PREC_NONE},
    [TOKEN_ERROR]           = {NULL,        NULL,       PREC_NONE},
    [TOKEN_EOF]             = {NULL,        NULL,       PREC_NONE},
};

const ParseRule *get_rule(TokenType type) {
    return &rules[type];
}
