#ifndef LUA_COMPILER_H
#define LUA_COMPILER_H

#include "common.h"
#include "chunk.h"
#include "compiler.h"
#include "lexer.h"
#include "object.h"

/**
 * III:17.2     Parsing Tokens
 * 
 * This struct works in tandem with `Lexer`. The Lexer emits raw tokens, and
 * the Parser keeps track of which token we have right now and which token we just
 * had. This kind of lookahead (or lookbehind) is just enough to parse complex
 * expressions.
 */
typedef struct {
    Token current;  // Token we're pointing at and want to consume.
    Token previous; // Token we just consumed.
    bool haderror;  // Track error state so we can report.
    bool panicking; // Track panic state so we don't vomit error cascades.
} Parser;

/**
 * The compiler manages state between the lexer and the parser, while emitting
 * bytecode. This is a lot to manage!
 * 
 * III:17.1     Single-Pass Compilation
 * 
 * It has 2 jobs: Parse the user's source code to understand what it means, and
 * emit low-level instruction (bytecode) based on how it understands the source
 * code.
 */
typedef struct {
    Chunk chunk; // This is where our raw bytecode resides.
    Lexer lexer; // Before generating bytecode, we need to poke at tokens.
    Parser parser; // Keep track of tokens emitted by `lexer`.
    LuaVM *vm; // Stupid but we need to pass this to `copy_string()` + friends.
} Compiler;

/**
 * III:17.2     Parsing Tokens
 * 
 * This function simply set's the compiler's parser's error and panic states to
 * false. Since we have a new compiler instance everytime we call `interpret_vm()`,
 * we (for now) assume to only set these at the start.
 * 
 * III:19.5     Freeing Objects
 * 
 * For our purposes a Lua virtual machine MUST be attached to the compiler.
 * For generating bytecode alone, this will be terrible...
 */
void init_compiler(Compiler *self, LuaVM *lvm);

/**
 * III:16.1.1   Opening the compilation pipeline
 * 
 * Instead of using a global Scanner instance like Robert does, I use pointers.
 * This resets the compiler's lexer so that we now begin compiling the source code
 * pointed to by `source`.
 * 
 * III:17       Compiling Expressions
 * 
 * In addition to the source code, we pass in a Chunk to emit the bytecode to.
 * So each Compiler instance is initailized with its own `Chunk` struct
 * when we call `interpret_vm()`.
 */
bool compile_bytecode(Compiler *self, const char *source);

#endif /* LUA_COMPILER_H */
