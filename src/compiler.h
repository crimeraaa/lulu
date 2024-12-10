#ifndef LULU_COMPILER_H
#define LULU_COMPILER_H

#include "lexer.h"
#include "chunk.h"

/**
 * @brief
 *      (2 ** 24) - 1 = 0b11111111_11111111_11111111
 */
#define LULU_MAX_CONSTANTS  ((1 << 24) - 1)

/**
 * @brief
 *      An assignable value, sometimes called an 'L-value'.
 */
typedef struct lulu_Assign lulu_Assign;
struct lulu_Assign {
    lulu_Assign *prev;  // Use recursion to chain multiple assignments.
    lulu_OpCode  op;    // GETGLOBAL, GETLOCAL, or GETTABLE.
    byte3        index; // Argument to 'op'.
};

typedef struct lulu_Parser lulu_Parser;
typedef struct lulu_Compiler lulu_Compiler;

struct lulu_Parser {
    lulu_Token     current;  // Also our "lookahead" token.
    lulu_Token     consumed; // Analogous to the book's `compiler.c:Parser::previous`.
    lulu_Assign   *assignments; // Must be valid only once per assignment call.
    lulu_Compiler *compiler;
    lulu_Lexer    *lexer;
};

struct lulu_Compiler {
    lulu_VM     *vm;    // Enclosing/parent state.
    lulu_Chunk  *chunk; // Destination for bytecode and constants.
    lulu_Parser *parser;
    lulu_Lexer  *lexer;
};

void
lulu_Compiler_init(lulu_VM *vm, lulu_Compiler *self);

/**
 * @note 2024-10-30
 *      Assumes 'self' has been initialized properly.
 */
void
lulu_Compiler_compile(lulu_Compiler *self, cstring input, lulu_Chunk *chunk);

void
lulu_Compiler_end(lulu_Compiler *self);

void
lulu_Compiler_emit_opcode(lulu_Compiler *self, lulu_OpCode op);

void
lulu_Compiler_emit_return(lulu_Compiler *self);

byte3
lulu_Compiler_make_constant(lulu_Compiler *self, const lulu_Value *value);

void
lulu_Compiler_emit_constant(lulu_Compiler *self, const lulu_Value *value);

void
lulu_Compiler_emit_byte1(lulu_Compiler *self, lulu_OpCode op, byte a);

void
lulu_Compiler_emit_byte3(lulu_Compiler *self, lulu_OpCode op, byte3 arg);

#endif // LULU_COMPILER_H
