#ifndef LULU_COMPILER_H
#define LULU_COMPILER_H

#include "lulu.h"
#include "lexer.h"
#include "chunk.h"

typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,    // =
    PREC_OR,            // or
    PREC_AND,           // and
    PREC_EQUALITY,      // == ~=
    PREC_COMPARISON,    // < <= >= >
    PREC_CONCAT,        // ..
    PREC_TERM,          // + -
    PREC_FACTOR,        // * / %
    PREC_UNARY,         // - # not
    PREC_POW,           // ^
    PREC_CALL,          // . ()
    PREC_PRIMARY,
} lulu_Precedence;

typedef struct {
    lulu_Token current;  // Also our "lookahead" token.
    lulu_Token consumed; // Analogous to the book's `compiler.c:Parser::previous`.
} lulu_Parser;

typedef struct {
    lulu_VM    *vm;    // Enclosing/parent state.
    lulu_Lexer *lexer; // To be shared across all nested compilers.
    lulu_Chunk *chunk; // Destination for bytecode and constants.
} lulu_Compiler;

typedef void (*lulu_ParseFn)(lulu_Compiler *self, lulu_Lexer *lexer, lulu_Parser *parser);

typedef const struct {
    lulu_ParseFn    prefix_fn;
    lulu_ParseFn    infix_fn;
    lulu_Precedence precedence;
} lulu_Parse_Rule;

void lulu_Compiler_init(lulu_VM *vm, lulu_Compiler *self, lulu_Lexer *lexer);
void lulu_Compiler_compile(lulu_Compiler *self, cstring input, lulu_Chunk *chunk);

#endif // LULU_COMPILER_H
