#ifndef LUA_COMPILER_H
#define LUA_COMPILER_H

#include "lua.h"
#include "lex.h"

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
