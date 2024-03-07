#include "parserules.h"

extern void grouping(Compiler *self, bool assignable);
extern void binary(Compiler *self, bool assignable);
extern void call(Compiler *self, bool assignable);
extern void rbinary(Compiler *self, bool assignable);
extern void unary(Compiler *self, bool assignable);
extern void number(Compiler *self, bool assignable);
extern void literal(Compiler *self, bool assignable);
extern void string(Compiler *self, bool assignable);
extern void variable(Compiler *self, bool assignable);
extern void and_(Compiler *self, bool assignable);
extern void or_(Compiler *self, bool assignable);

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
static const ParseRule rules[TK_COUNT] = {
    // Single character tokens
    [TK_LPAREN]          = {grouping,    call,       PREC_CALL},
    [TK_RPAREN]          = {NULL,        NULL,       PREC_NONE},
    [TK_LBRACE]          = {NULL,        NULL,       PREC_NONE},
    [TK_RBRACE]          = {NULL,        NULL,       PREC_NONE},
    [TK_LBRACKET]        = {NULL,        NULL,       PREC_NONE},
    [TK_RBRACKET]        = {NULL,        NULL,       PREC_NONE},
    [TK_COMMA]           = {NULL,        NULL,       PREC_NONE},
    [TK_PERIOD]          = {NULL,        NULL,       PREC_NONE},
    [TK_COLON]           = {NULL,        NULL,       PREC_NONE},
    [TK_POUND]           = {NULL,        NULL,       PREC_NONE},
    [TK_SEMICOL]         = {NULL,        NULL,       PREC_NONE},
    [TK_ASSIGN]          = {NULL,        NULL,       PREC_NONE},
    
    // Arithmetic Operators
    [TK_PLUS]            = {NULL,        binary,     PREC_TERMINAL},
    [TK_DASH]            = {unary,       binary,     PREC_TERMINAL},
    [TK_STAR]            = {NULL,        binary,     PREC_FACTOR},
    [TK_SLASH]           = {NULL,        binary,     PREC_FACTOR},
    [TK_CARET]           = {NULL,        rbinary,    PREC_EXPONENT},
    [TK_PERCENT]         = {NULL,        binary,     PREC_FACTOR},
    
    // Relational Operators
    [TK_EQ]              = {NULL,        binary,     PREC_EQUALITY},
    [TK_NEQ]             = {NULL,        binary,     PREC_EQUALITY},
    [TK_GT]              = {NULL,        binary,     PREC_COMPARISON},
    [TK_GE]              = {NULL,        binary,     PREC_COMPARISON},
    [TK_LT]              = {NULL,        binary,     PREC_COMPARISON},
    [TK_LE]              = {NULL,        binary,     PREC_COMPARISON},

    // Literals
    [TK_FALSE]           = {literal,     NULL,       PREC_NONE},
    [TK_IDENT]           = {variable,    NULL,       PREC_NONE},
    [TK_NIL]             = {literal,     NULL,       PREC_NONE},
    [TK_NUMBER]          = {number,      NULL,       PREC_NONE},
    [TK_STRING]          = {string,      NULL,       PREC_NONE},
    [TK_TABLE]           = {NULL,        NULL,       PREC_NONE},
    [TK_TRUE]            = {literal,     NULL,       PREC_NONE},

    // Keywords
    [TK_AND]             = {NULL,        and_,       PREC_AND},
    [TK_BREAK]           = {NULL,        NULL,       PREC_NONE},
    [TK_DO]              = {NULL,        NULL,       PREC_NONE},
    [TK_ELSE]            = {NULL,        NULL,       PREC_NONE},
    [TK_ELSEIF]          = {NULL,        NULL,       PREC_NONE},
    [TK_END]             = {NULL,        NULL,       PREC_NONE},
    [TK_FOR]             = {NULL,        NULL,       PREC_NONE},
    [TK_FUNCTION]        = {NULL,        NULL,       PREC_NONE},
    [TK_IF]              = {NULL,        NULL,       PREC_NONE},
    [TK_IN]              = {NULL,        NULL,       PREC_NONE},
    [TK_LOCAL]           = {NULL,        NULL,       PREC_NONE},
    [TK_NOT]             = {unary,       NULL,       PREC_NONE},
    [TK_OR]              = {NULL,        or_,        PREC_OR},
    [TK_RETURN]          = {NULL,        NULL,       PREC_NONE},
    [TK_SELF]            = {NULL,        NULL,       PREC_NONE},
    [TK_THEN]            = {NULL,        NULL,       PREC_NONE},
    [TK_WHILE]           = {NULL,        NULL,       PREC_NONE},

    // Misc.
    [TK_CONCAT]          = {NULL,        rbinary,    PREC_CONCAT},
    [TK_VARARGS]         = {NULL,        NULL,       PREC_NONE},
    [TK_ERROR]           = {NULL,        NULL,       PREC_NONE},
    [TK_EOF]             = {NULL,        NULL,       PREC_NONE},
};

const ParseRule *get_rule(TokenType type) {
    return &rules[type];
}
