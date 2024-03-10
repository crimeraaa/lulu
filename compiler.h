#ifndef LUA_COMPILER_H
#define LUA_COMPILER_H

#include "common.h"
#include "chunk.h"
#include "lexstate.h"
#include "object.h"

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
 * III:24.2     Compiling to Function Objects
 * 
 * Help the compiler differentiate between top-level code (i.e. a script) versus 
 * a function. Remember that even jlox needed this!
 */
typedef enum {
    FNTYPE_FUNCTION,
    FNTYPE_SCRIPT,
} FnType;

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
 * 
 * III:24.2     Compiling to Function Objects
 * 
 * Before our compiler assumes that it was only ever compiling one chunk. With
 * functions and their own separate chunks that system falls apart. So when we,
 * the compiler, reach a function declaration, we emit code into that function's
 * chunk when compiling the body. Then at the end of the function body we have
 * to return to the PREVIOUS chunk we were working with.
 * 
 * But in order for THAT to work we need to make the compiler always work within
 * a function body, even at top-level scope. Think of it as the entire Lua script
 * being wrapped in an implicit `main` function.
 */
typedef struct Compiler {
    struct Compiler *enclosing; // Nested function calls as a stack/linked list.
    TFunction *function; // Contains the chunk we're currently compiling.
    FnType type;    // Function type to differentiate from the top-level script.
    LexState *lex;  // Maintain pointers to the source code and emit tokens.
    Locals locals;  // Keep track of information about local variables in scope.
    LVM *vm;        // Stupid but we need to pass this to `copy_string()`.
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
 * 
 * III:24.2     Compiling to Function Objects
 * 
 * We now have to specify what kind of "function" we're compiling at the start.
 * 
 * III:24.4.1   A stack of compilers
 * 
 * In order to allow deeply and also recursive functions calls, we treat them
 * like a stack using recursion and the C stack as our "linked list" of functions
 * to push and pop.
 */
void init_compiler(Compiler *self, Compiler *current, LVM *vm, FnType type);

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
 * 
 * III:24.2     Compiling to Function Objects
 * 
 * We now return a function object pointer, using `NULL` to signal errors.
 * 
 * III:24.5.4   Returning from functions
 * 
 * Since we now have a sort of "shared" state across scripts and their chunks,
 * we don't need the `source` parameter anymore as the `LexState*` is shared
 * and it points to the correct stuff.
 */
TFunction *compile_bytecode(Compiler *self);

#endif /* LUA_COMPILER_H */
