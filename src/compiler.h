/**
 * @brief   The Compiler handles code generation. For precedences and such, that
 *          is handled by `parser.h` which is independent of this file although
 *          all the parser functions manipulate their Compiler struct.
 */
#ifndef LULU_COMPILER_H
#define LULU_COMPILER_H

#include "lulu.h"
#include "chunk.h"
#include "lexer.h"

// Defined in `parser.h`.
typedef struct Assignment Assignment;

typedef struct {
    Token name; // Identifier in the source code.
    int   depth;  // Scope depth at the time of declaration.
} Local;

typedef struct {
    Local  locals[MAX_LOCALS];
    Lexer *lexer;       // May be shared across multiple Compiler instances.
    VM    *vm;          // Track and modify parent VM state as needed.
    Chunk *chunk;       // The current compiling chunk for this function/closure.
    int    local_count; // How many locals are currently in scope?
    int    scope_depth; // 0 = global, 1 = top-level, 2 = more inner, etc.
    int    stack_total; // How many stack slots at most does this function use?
    int    stack_usage; // How many stack slots are currently being used?
    OpCode prev_opcode; // Used to fold consecutive similar operations.
} Compiler;

// We pass a Lexer and a VM to be shared across compiler instances.
void init_compiler(Compiler *self, Lexer *lexer, VM *vm);
void end_compiler(Compiler *self);
void compile(Compiler *self, const char *input, Chunk *chunk);

Chunk *current_chunk(Compiler *self);
void emit_opcode(Compiler *self, OpCode op);
void emit_oparg1(Compiler *self, OpCode op, Byte arg);
void emit_oparg2(Compiler *self, OpCode op, Byte2 arg);
void emit_oparg3(Compiler *self, OpCode op, Byte3 arg);
void emit_return(Compiler *self);
void emit_fields(Compiler *self, Assignment *list, int *nest);
void emit_gettable(Compiler *self, Assignment *list, int *nest);

// Returns the index of `value` in the constants table.
// Will throw if our current number of constants exceeds `MAX_CONSTS`.
int make_constant(Compiler *self, const TValue *value);
void emit_constant(Compiler *self, const TValue *value);

// Intern the `TString*` for `name` so we can easily look it up later.
int identifier_constant(Compiler *self, const Token *name);

void begin_scope(Compiler *self);
void end_scope(Compiler *self);

// Analogous to `defineVariable()` in the book, but for a comma-separated list
// form `'local' identifier [, identifier]* [';']`.
// We considered "defined" local variables to be ready for reading/writing.
void define_locals(Compiler *self, int count);

// Analogous to `declareVariable()` in the book, but only for Lua locals.
// Assumes we just consumed a local variable identifier token.
void init_local(Compiler *self);

// Initializes the current top of the locals array.
// Returns index of newly initialized local into the locals array.
void add_local(Compiler *self, const Token *name);

// Returns index of a local variable or -1 if assumed to be global.
int resolve_local(Compiler *self, const Token *name);

#endif /* LULU_COMPILER_H */
