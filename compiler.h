#ifndef LUA_COMPILER_H
#define LUA_COMPILER_H

#include "common.h"
#include "lexer.h"

/**
 * The compiler manages state between the lexer and the parser, while emitting
 * bytecode. This is a lot to manage!
 */
typedef struct {
    LuaLexer lexer; // Before generating bytecode, we need to poke at tokens.
} LuaCompiler;

/**
 * This function is my addition, and for the time being does absolutely nothing.
 */
void init_compiler(LuaCompiler *self);

/**
 * III:16.1.1   Opening the compilation pipeline
 * 
 * Instead of using a global Scanner instance like Robert does, I use pointers.
 * This resets the compiler's lexer so that we now begin compiling the source code
 * pointed to by `source`.
 */
void compile_bytecode(LuaCompiler *self, const char *source);

#endif /* LUA_COMPILER_H */
