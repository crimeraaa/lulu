#ifndef LUA_COMPILER_H
#define LUA_COMPILER_H

#include "common.h"
#include "chunk.h"
#include "lexer.h"
#include "object.h"
#include "parser.h"

typedef struct JumpList {
    struct JumpList *next; // Help us to chain.
    size_t jump;     // Index into code array of the jump instruction.
} JumpList;

/**
 * III:22.1     Representing Local Variables
 * 
 * Since we can store local variables on the VM's stack, we just need the right
 * instructions and information to verify that a particular element in the stack
 * indeed matches a particular local variable we're after.
 * 
 * So we just store the name and scope depth at the time the variable was
 * declared.
 */
typedef struct {
    Token name; // Name of the identifier, used for variable resolution.
    int depth;  // Scope depth at time of declaration.
} Local;

/**
 * III:22.2     Representing Local Variables
 * 
 * This struct actually takes care of keeping track of how many locals, how far
 * down in scope we are and a pseudo-stack for their actual values.
 * 
 * To "allocate" memory we simply push to the stack. 
 * Likewise, to "deallocate" memory, we simply pop the stack.
 */
typedef struct {
    Local stack[LUA_MAXLOCALS]; // Stack/List of locals in scope at this point.
    int count; // How many locals are currently in scope?
    int depth; // Scope depth: how many blocks surround us?
               // 0 means global scope, 1 means 1st top-level block scope, etc.
} Locals;

/**
 * The compiler manages state between the lexer and the parser, while emitting
 * bytecode. This is a lot to manage!
 * 
 * III:17.1     Single-Pass Compilation
 * 
 * It has 2 jobs: Parse the user's source code to understand what it means, and
 * emit low-level instruction (bytecode) based on how it understands the source
 * code.
 * 
 * III:22.4     Using Locals
 * 
 * I've separated the `Parser` struct into its own separate module so that the
 * `compiler.c` file is less crowded.
 */
typedef struct {
    Chunk chunk;    // This is where our raw bytecode resides.
    Parser parser;  // Keep track of tokens emitted by its own `Lexer`.
    Locals locals;  // Keep track of information about local variables in scope.
    lua_VM *vm;     // Stupid but we need to pass this to `copy_string()`.
    JumpList *breaks;
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
 * 
 * III:22.1     Representing Local Variables
 * 
 * Now with the addition of new members we also 0-initialize them so that the
 * compiler starts out with no local variables in scope and no surrounding scope
 * blocks.
 */
void init_compiler(Compiler *self, lua_VM *lvm);

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
