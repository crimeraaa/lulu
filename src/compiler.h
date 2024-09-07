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

typedef struct lulu_Parser lulu_Parser;
typedef struct {
    lulu_VM    *vm;    // Enclosing/parent state.
    lulu_Lexer *lexer; // To be shared across all nested compilers.
    lulu_Chunk *chunk; // Destination for bytecode and constants.
} lulu_Compiler;

void lulu_Compiler_init(lulu_VM *vm, lulu_Compiler *self, lulu_Lexer *lexer);
void lulu_Compiler_compile(lulu_Compiler *self, cstring input, lulu_Chunk *chunk);
void lulu_Compiler_end(lulu_Compiler *self, lulu_Parser *parser);

void lulu_Compiler_emit_byte(lulu_Compiler *self, lulu_Parser *parser, byte inst);
void lulu_Compiler_emit_bytes(lulu_Compiler *self, lulu_Parser *parser, byte inst1, byte inst2);
void lulu_Compiler_emit_byte3(lulu_Compiler *self, lulu_Parser *parser, usize byte3);
void lulu_Compiler_emit_return(lulu_Compiler *self, lulu_Parser *parser);

void lulu_Compiler_emit_constant(lulu_Compiler *self, lulu_Parser *parser, const lulu_Value *value);

#endif // LULU_COMPILER_H
