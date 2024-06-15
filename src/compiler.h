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

typedef struct {
    Token ident; // Identifier in the source code.
    int   depth; // Scope depth at the time of declaration.
} Local;

typedef struct {
    Local    locals[MAX_LOCALS];
    Lexer   *lexer;       // Shared across multiple Compiler instances.
    lulu_VM *vm;          // Track and modify parent VM state as needed.
    Chunk   *chunk;       // Chunk for this function/closure.
    int      stack_usage; // #stack slots being use currently.
    int      stack_total; // maximum #stack-slots used.
    int      scope_count; // How many locals are currently in scope?
    int      scope_depth; // 0 = global, 1 = top, 2 = more inner, etc.
    OpCode   prev_opcode; // Used to fold consecutive similar operations.
} Compiler;

// We pass a Lexer and a VM to be shared across compiler instances.
void luluCpl_init(Compiler *cpl, Lexer *ls, lulu_VM *vm);
void luluCpl_end(Compiler *cpl);
void luluCpl_compile(Compiler *cpl, const char *input, Chunk *chunk);

void luluCpl_emit_opcode(Compiler *cpl, OpCode op);
void luluCpl_emit_oparg1(Compiler *cpl, OpCode op, Byte arg);
void luluCpl_emit_oparg2(Compiler *cpl, OpCode op, Byte2 arg);
void luluCpl_emit_oparg3(Compiler *cpl, OpCode op, Byte3 arg);
void luluCpl_emit_return(Compiler *cpl);
void luluCpl_emit_identifier(Compiler *cpl, const Token *id);

// Returns the index of `OP_NEWTABLE` in the bytecode. We cannot use pointers
// due to potential invalidation when reallocating the array.
int luluCpl_emit_table(Compiler *cpl);
void luluCpl_patch_table(Compiler *cpl, int offset, Byte3 size);

// Returns the index of `v` in the constants table.
// Will throw if our current number of constants exceeds `MAX_CONSTS`.
int luluCpl_make_constant(Compiler *cpl, const Value *vl);
void luluCpl_emit_constant(Compiler *cpl, const Value *vl);
void luluCpl_emit_variable(Compiler *cpl, const Token *id);

// Intern the `String*` for `name` so we can easily look it up later.
int luluCpl_identifier_constant(Compiler *cpl, const Token *id);

void luluCpl_begin_scope(Compiler *cpl);
void luluCpl_end_scope(Compiler *cpl);

// Analogous to `defineVariable()` in the book, but for a comma-separated list
// form `'local' identifier [, identifier]* [';']`.
// We considered "defined" local variables to be ready for reading/writing.
void luluCpl_define_locals(Compiler *cpl, int count);

// Analogous to `declareVariable()` in the book, but only for Lua locals.
// Assumes we just consumed a local variable identifier token.
void luluCpl_init_local(Compiler *cpl);

// Initializes the current top of the locals array.
// Returns index of newly initialized local into the locals array.
void luluCpl_add_local(Compiler *cpl, const Token *id);

// Returns index of a local variable or -1 if assumed to be global.
int luluCpl_resolve_local(Compiler *cpl, const Token *id);

#endif /* LULU_COMPILER_H */
