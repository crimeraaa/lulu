/**
 * Do NOT include this file in `compiler.h`. It may result in circular dependencies. 
 */
#ifndef LUA_PRECEDENCE_H
#define LUA_PRECEDENCE_H

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
} LuaPrecedence;

typedef void (*LuaParseFn)(LuaCompiler *self);

typedef struct {
    LuaParseFn prefix;
    LuaParseFn infix;
    LuaPrecedence precedence;
} LuaParseRule;

const LuaParseRule *get_rule(LuaTokenType type);

#endif /* LUA_PRECEDENCE_H */
