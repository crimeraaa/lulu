#ifndef LUA_COMPILER_H
#define LUA_COMPILER_H

#include "lua.h"
#include "lex.h"

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT, // =
    PREC_OR,         // or
    PREC_AND,        // and
    PREC_EQUALITY,   // == ~=
    PREC_COMPARISON, // < > <= >=
    PREC_TERMINAL,   // + -
    PREC_FACTOR,     // * /
    PREC_UNARY,      // - not
    PREC_CALL,       // . ()
    PREC_PRIMARY,
} Precedence;

typedef void (*ParseFn)(Compiler *self);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence prec;
} ParseRule;

struct Compiler {
    lua_VM *vm;  // Track and adjust primary VM state as needed.
    Lexer *lex;  // May be shared across multiple instances.
    int freereg; // Index of first free register in the VM.
};

void init_compiler(Compiler *self, lua_VM *vm, Lexer *lex);

// Create bytecode, instructions, constants, etc. for the given `Compiler`.
// May longjmp at any point in the parsing/compiling process.
bool compile(Compiler *self, const char *input);

#endif /* LUA_COMPILER_H */
