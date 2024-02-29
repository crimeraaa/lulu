#ifndef LUA_PARSERULES_H
#define LUA_PARSERULES_H

#include "common.h"
#include "compiler.h"
/** 
 * III:17.4.3   Unary negation
 * 
 * Unlike our other enums, the order for this DOES matter. This precedences are
 * listed from lowest/weakest precedence to highest/strongest precedence.
 * 
 * For Lua 5.1, see section 2.5.6: https://www.lua.org/manual/5.1/manual.html
 * 
 * Unless otherwise specified each of these precedences are left associative.
 * Meaning operations of the same precedence evaluate from left to right.
 */
typedef enum {
    PREC_NONE,          // De-facto base case for our recursive Pratt parser.
    PREC_ASSIGNMENT,    // =
    PREC_OR,            // or
    PREC_AND,           // and
    PREC_EQUALITY,      // == ~=
    PREC_COMPARISON,    // < > <= >=
    PREC_CONCAT,        // .. is right associative.
    PREC_TERMINAL,      // + -
    PREC_FACTOR,        // / * %
    PREC_UNARY,         // - not #
    PREC_EXPONENT,      // ^ is right associative.
    PREC_CALL,          // . : ()
    PREC_PRIMARY,
} Precedence;

typedef void (*ParseFn)(Compiler *self);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

/**
 * This function is just a wrapper around the internal `rules` lookup table.
 * It's such a beast that Bob, understandably, prefers to hide its implementation
 * via this function.
 */
const ParseRule *get_rule(TokenType type);

#else /* LUA_PARSERULES_H defined. */
#error "`parserules.h` can only be used inside of `compiler.c`."
#endif /* LUA_PARSERULES_H */
