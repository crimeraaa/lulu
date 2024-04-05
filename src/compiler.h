#ifndef LULU_COMPILER_H
#define LULU_COMPILER_H

#include "lulu.h"
#include "lexer.h"

typedef struct {
    Lexer *lexer; // May be shared across multiple Compiler instances.
    VM *vm;       // Track and modify parent VM state as needed.
} Compiler;

void init_compiler(Compiler *self, Lexer *lexer, VM *vm);

void compile(Compiler *self, const char *input);

#endif /* LULU_COMPILER_H */
