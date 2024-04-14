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

typedef struct Compiler {
    Lexer *lexer; // May be shared across multiple Compiler instances.
    VM *vm;       // Track and modify parent VM state as needed.
    Chunk *chunk; // The current compiling chunk for this function/closure.
} Compiler;

// We pass a Lexer and a VM to be shared across compiler instances.
void init_compiler(Compiler *self, Lexer *lexer, VM *vm);
void compile(Compiler *self, const char *input, Chunk *chunk);
void emit_byte(Compiler *self, Byte data);
void emit_byte2(Compiler *self, Byte2 data);
void emit_byte3(Compiler *self, Byte3 data);
void emit_return(Compiler *self);

// Returns the index of `value` in the constants table.
int make_constant(Compiler *self, const TValue *value);
void emit_constant(Compiler *self, const TValue *value);
void end_compiler(Compiler *self);

#endif /* LULU_COMPILER_H */
