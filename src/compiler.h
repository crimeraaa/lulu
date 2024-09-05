#ifndef LULU_COMPILER_H
#define LULU_COMPILER_H

#include "lulu.h"
#include "lexer.h"

typedef struct {
    lulu_VM *vm;
} lulu_Compiler;

void lulu_Compiler_compile(lulu_Compiler *self, lulu_Lexer *lex, cstring input);

#endif // LULU_COMPILER_H
