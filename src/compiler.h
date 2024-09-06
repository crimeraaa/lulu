#ifndef LULU_COMPILER_H
#define LULU_COMPILER_H

#include "lulu.h"
#include "lexer.h"

typedef struct {
    lulu_VM    *vm;    // Enclosing/parent state.
    lulu_Lexer *lexer; // To be shared across all nested compilers.
} lulu_Compiler;

void lulu_Compiler_init(lulu_VM *vm, lulu_Compiler *self, lulu_Lexer *lexer);
void lulu_Compiler_compile(lulu_Compiler *self, cstring input);

#endif // LULU_COMPILER_H
