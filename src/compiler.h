#ifndef LUA_COMPILER_H
#define LUA_COMPILER_H

#include "lua.h"
#include "lexer.h"

typedef struct Compiler Compiler;

struct Compiler {
    LexState *lexstate; // May be shared across multiple instances.
};

void compile(Compiler *self, const char *input);

#endif /* LUA_COMPILER_H */
