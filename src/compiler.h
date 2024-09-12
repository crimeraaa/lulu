#ifndef LULU_COMPILER_H
#define LULU_COMPILER_H

#include "lulu.h"
#include "lexer.h"
#include "chunk.h"

/**
 * @brief
 *      (2 ** 24) - 1 = 0b11111111_11111111_11111111
 */
#define LULU_MAX_CONSTANTS  ((1 << 24) - 1)

typedef struct {
    lulu_Token current;  // Also our "lookahead" token.
    lulu_Token consumed; // Analogous to the book's `compiler.c:Parser::previous`.
} lulu_Parser;

typedef struct {
    lulu_VM    *vm;    // Enclosing/parent state.
    lulu_Chunk *chunk; // Destination for bytecode and constants.
    lulu_OpCode prev_opcode; // Used for single-pass (simple) optimization.
} lulu_Compiler;

void
lulu_Compiler_init(lulu_VM *vm, lulu_Compiler *self);

void
lulu_Compiler_compile(lulu_Compiler *self, cstring input, lulu_Chunk *chunk);

void
lulu_Compiler_end(lulu_Compiler *self, lulu_Parser *parser);

void
lulu_Compiler_emit_opcode(lulu_Compiler *self, lulu_Parser *parser, lulu_OpCode opcode);

void
lulu_Compiler_emit_return(lulu_Compiler *self, lulu_Parser *parser);

void
lulu_Compiler_emit_constant(lulu_Compiler *self, lulu_Lexer *lexer, lulu_Parser *parser, const lulu_Value *value);

#endif // LULU_COMPILER_H
