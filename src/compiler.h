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

#define CAN_ASSIGN (true)

typedef struct {
    String *identifier; // Interned by Lexer.
    int     depth;      // Scope depth at the time of declaration.
} Local;

typedef struct {
    Local locals[LULU_MAX_LOCALS];
    int   count; // How many locals are *currently* in scope?
    int   depth; // 0 = global, 1 = top-level, 2 = more innter, etc.
} Scope;

typedef enum {
    JUMP_FORWARD,
    JUMP_BACKWARD,
} JumpType;

typedef struct {
    Scope    scope;
    Lexer   *lexer;       // Shared across multiple Compiler instances.
    lulu_VM *vm;          // Track and modify parent VM state as needed.
    Chunk   *chunk;       // Chunk for this function/closure.
    int      stack_usage; // #stack slots being use currently.
    int      stack_total; // maximum #stack-slots used.
    OpCode   prev_opcode; // Used to fold consecutive similar operations.
    bool     can_fold;    // Really stupid but need separate POPs for 'if'.
} Compiler;

void luluCpl_init_compiler(Compiler *cpl, lulu_VM *vm);
void luluCpl_end_compiler(Compiler *cpl);
void luluCpl_compile(Compiler *cpl, Lexer *ls, Chunk *chunk);

void luluCpl_emit_opcode(Compiler *cpl, OpCode op);
void luluCpl_emit_oparg1(Compiler *cpl, OpCode op, Byte arg);
void luluCpl_emit_oparg2(Compiler *cpl, OpCode op, Byte2 arg);
void luluCpl_emit_oparg3(Compiler *cpl, OpCode op, Byte3 arg);
void luluCpl_emit_return(Compiler *cpl);
void luluCpl_patch_byte3(Compiler *cpl, int offset, Byte3 arg);
Byte3 luluCpl_get_byte3(Compiler *cpl, int offset);

// Returns the index of `OP_JUMP` in the bytecode.
int   luluCpl_emit_jump(Compiler *cpl);
int   luluCpl_emit_if_jump(Compiler *cpl);
Byte3 luluCpl_get_jump(Compiler *cpl, int offset, JumpType type);
void  luluCpl_patch_jump(Compiler *cpl, int offset);

// Returns the index of the first instruction related to a loop body.
int  luluCpl_start_loop(Compiler *cpl);
void luluCpl_emit_loop(Compiler *cpl, int loop_start);

// Returns the index of `OP_NEWTABLE` in the bytecode.
int  luluCpl_emit_table(Compiler *cpl);
void luluCpl_patch_table(Compiler *cpl, int offset, Byte3 size);

// Returns the index of `v` in the constants table.
// Will throw if our current number of constants exceeds `MAX_CONSTS`.
int  luluCpl_make_constant(Compiler *cpl, const Value *vl);
void luluCpl_emit_constant(Compiler *cpl, const Value *vl);
void luluCpl_emit_variable(Compiler *cpl, String *id);
void luluCpl_emit_identifier(Compiler *cpl, String *id);
int  luluCpl_identifier_constant(Compiler *cpl, String *id);

void luluCpl_begin_scope(Compiler *cpl);
void luluCpl_end_scope(Compiler *cpl);

// Analogous to `defineVariable()` in the book, but for a comma-separated list
// form `'local' identifier [, identifier]* [';']`.
// We considered "defined" local variables to be ready for reading/writing.
void luluCpl_define_locals(Compiler *cpl, int count);

// Analogous to `declareVariable()` in the book, but only for Lua locals.
// Assumes we just consumed a local variable identifier token.
void luluCpl_init_local(Compiler *cpl, String *id);

// Initializes the current top of the locals array.
// Returns index of newly initialized local into the locals array.
void luluCpl_add_local(Compiler *cpl, String *id);

// Returns index of a local variable or -1 if assumed to be global.
int luluCpl_resolve_local(Compiler *cpl, String *id);

#endif /* LULU_COMPILER_H */
