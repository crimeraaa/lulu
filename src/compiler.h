#ifndef LULU_COMPILER_H
#define LULU_COMPILER_H

#include "lulu.h"
#include "chunk.h"
#include "lexer.h"

#define LULU_ERROR_NONE     0
#define LULU_ERROR_COMPTIME 1
#define LULU_ERROR_RUNTIME  2

typedef struct Compiler {
    Lexer *lexer; // May be shared across multiple Compiler instances.
    VM *vm;       // Track and modify parent VM state as needed.
    Chunk *chunk; // The current compiling chunk for this function/closure.
} Compiler;

typedef enum {
    PREC_NONE,
    PREC_ASSIGN,     // `=`
    PREC_OR,         // `or`
    PREC_AND,        // `and`
    PREC_EQUALITY,   // `==` and `!=`
    PREC_COMPARISON, // `<`, `>`, `<=` and `>=`
    PREC_TERMINAL,   // `+` and `-`
    PREC_FACTOR,     // `*`, `/`, `%` and `^`
    PREC_UNARY,      // `not`, `-` and `#`
    PREC_CALL,       // `.` `()`
    PREC_PRIMARY,
} Precedence;

typedef void (*ParseFn)(Compiler *self);

typedef struct {
    ParseFn prefixfn;
    ParseFn infixfn;
    Precedence prec;
} ParseRule;

// We pass a Lexer and a VM to be shared across compiler instances.
void init_compiler(Compiler *self, Lexer *lexer, VM *vm);

void compile(Compiler *self, const char *input, Chunk *chunk);

#endif /* LULU_COMPILER_H */
