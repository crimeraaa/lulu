#include "precedence.h"

extern void grouping(LuaCompiler *self);
extern void binary(LuaCompiler *self);
extern void unary(LuaCompiler *self);
extern void number(LuaCompiler *self);

/**
 * III:17.6     A Pratt Parser
 * 
 * This thing is huge! It mirrors the `LuaTokenType` enum.
 * 
 * NOTE:
 * 
 * Because we need to access functions that are not defined here and are also not
 * declared in `compiler.h`, we NEED them to be non-static in `compiler.c`.
 * 
 * This is so we can link to them properly. If they were static, the linker would
 * not be able to resolve the symbols here.
 */
static const LuaParseRule rules[TOKEN_COUNT] = {
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
    [TOKEN_EQUAL]           = {NULL,        NULL,       PREC_NONE},
    
    // Arithmetic Operators
    [TOKEN_PLUS]            = {NULL,        binary,     PREC_TERMINAL},
    [TOKEN_DASH]            = {unary,       binary,     PREC_TERMINAL},
    [TOKEN_STAR]            = {NULL,        binary,     PREC_FACTOR},
    [TOKEN_SLASH]           = {NULL,        binary,     PREC_FACTOR},
    [TOKEN_CARET]           = {NULL,        binary,     PREC_EXPONENT},
    [TOKEN_PERCENT]         = {NULL,        binary,     PREC_FACTOR},
    
    // Relational Operators
    [TOKEN_REL_EQ]          = {NULL,        NULL,       PREC_NONE},
    [TOKEN_REL_NEQ]         = {NULL,        NULL,       PREC_NONE},
    [TOKEN_REL_GT]          = {NULL,        NULL,       PREC_NONE},
    [TOKEN_REL_GE]          = {NULL,        NULL,       PREC_NONE},
    [TOKEN_REL_LT]          = {NULL,        NULL,       PREC_NONE},
    [TOKEN_REL_LE]          = {NULL,        NULL,       PREC_NONE},

    // Literals
    [TOKEN_FALSE]           = {NULL,        NULL,       PREC_NONE},
    [TOKEN_IDENT]           = {NULL,        NULL,       PREC_NONE},
    [TOKEN_NIL]             = {NULL,        NULL,       PREC_NONE},
    [TOKEN_NUMBER]          = {number,      NULL,       PREC_NONE},
    [TOKEN_STRING]          = {NULL,        NULL,       PREC_NONE},
    [TOKEN_TABLE]           = {NULL,        NULL,       PREC_NONE},
    [TOKEN_TRUE]            = {NULL,        NULL,       PREC_NONE},

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
    [TOKEN_NOT]             = {NULL,        NULL,       PREC_NONE},
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

const LuaParseRule *get_rule(LuaTokenType type) {
    return &rules[type];
}
