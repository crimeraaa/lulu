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
void luluComp_init(Compiler *comp, Lexer *ls, lulu_VM *vm);
void luluComp_end(Compiler *comp);
void luluComp_compile(Compiler *comp, const char *input, Chunk *chunk);

void luluComp_emit_opcode(Compiler *comp, OpCode op);
void luluComp_emit_oparg1(Compiler *comp, OpCode op, Byte arg);
void luluComp_emit_oparg2(Compiler *comp, OpCode op, Byte2 arg);
void luluComp_emit_oparg3(Compiler *comp, OpCode op, Byte3 arg);
void luluComp_emit_return(Compiler *comp);
void luluComp_emit_identifier(Compiler *comp, const Token *id);

// Returns the index of `OP_NEWTABLE` in the bytecode. We cannot use pointers
// due to potential invalidation when reallocating the array.
int luluComp_emit_table(Compiler *comp);
void luluComp_patch_table(Compiler *comp, int offset, Byte3 size);

// Returns the index of `v` in the constants table.
// Will throw if our current number of constants exceeds `MAX_CONSTS`.
int luluComp_make_constant(Compiler *comp, const Value *vl);
void luluComp_emit_constant(Compiler *comp, const Value *vl);
void luluComp_emit_variable(Compiler *comp, const Token *id);

// Intern the `String*` for `name` so we can easily look it up later.
int luluComp_identifier_constant(Compiler *comp, const Token *id);

void luluComp_begin_scope(Compiler *comp);
void luluComp_end_scope(Compiler *comp);

// Analogous to `defineVariable()` in the book, but for a comma-separated list
// form `'local' identifier [, identifier]* [';']`.
// We considered "defined" local variables to be ready for reading/writing.
void luluComp_define_locals(Compiler *comp, int count);

// Analogous to `declareVariable()` in the book, but only for Lua locals.
// Assumes we just consumed a local variable identifier token.
void luluComp_init_local(Compiler *comp);

// Initializes the current top of the locals array.
// Returns index of newly initialized local into the locals array.
void luluComp_add_local(Compiler *comp, const Token *id);

// Returns index of a local variable or -1 if assumed to be global.
int luluComp_resolve_local(Compiler *comp, const Token *id);

#endif /* LULU_COMPILER_H */
