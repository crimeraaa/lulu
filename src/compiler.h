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
    Token name; // Identifier in the source code.
    int depth;  // Scope depth at the time of declaration.
} Local;

typedef struct {
    Local locals[MAX_LOCALS];
    Lexer *lexer; // May be shared across multiple Compiler instances.
    VM *vm;       // Track and modify parent VM state as needed.
    Chunk *chunk; // The current compiling chunk for this function/closure.
    int localcount; // How many locals are currently in scope?
    int scopedepth; // 0 = global, 1 = top-level, 2 = more inner, etc.
} Compiler;

// We pass a Lexer and a VM to be shared across compiler instances.
void init_compiler(Compiler *self, Lexer *lexer, VM *vm);
void end_compiler(Compiler *self);
void compile(Compiler *self, const char *input, Chunk *chunk);

void emit_byte(Compiler *self, Byte data);
void emit_byte2(Compiler *self, OpCode opcode, Byte2 data);
void emit_byte3(Compiler *self, OpCode opcode, Byte3 data);
void emit_bytes(Compiler *self, Byte data1, Byte data2);
void emit_return(Compiler *self);

// Returns the index of `value` in the constants table.
int make_constant(Compiler *self, const TValue *value);
void emit_constant(Compiler *self, const TValue *value);

// Intern the `TString*` for `name` so we can easily look it up later.
int identifier_constant(Compiler *self, const Token *name);

void begin_scope(Compiler *self);
void end_scope(Compiler *self);

#endif /* LULU_COMPILER_H */
