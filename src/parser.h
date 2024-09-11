#ifndef LULU_PARSER_H
#define LULU_PARSER_H

#include "lulu.h"
#include "compiler.h"
#include "lexer.h"

#include <stdnoreturn.h>

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

typedef void
(*lulu_ParseFn)(lulu_Compiler *compiler, lulu_Lexer *lexer, lulu_Parser *parser);

typedef struct {
    lulu_ParseFn    prefix_fn;
    lulu_ParseFn    infix_fn;
    lulu_Precedence precedence;
} lulu_Parse_Rule;

/**
 * @note 2024-09-06
 *      Analogous to the book's `compiler.c:advance()`.
 */
void
lulu_Parse_advance_token(lulu_Lexer *lexer, lulu_Parser *parser);

/**
 * @note 2024-09-07
 *      Analogous to the book's `compiler.c:consume()`.
 */
void
lulu_Parse_consume_token(lulu_Lexer *lexer, lulu_Parser *parser, lulu_Token_Type type, cstring msg);

void
lulu_Parse_expression(lulu_Compiler *compiler, lulu_Lexer *lexer, lulu_Parser *parser);

/**
 * @note 2024-09-07
 *      Analogous to the book's `compiler.c:errorCurrent()`.
 */
noreturn void
lulu_Parse_error_current(lulu_VM *vm, lulu_Parser *parser, cstring msg);

/**
 * @note 2024-09-07
 *      Analogous to the book's `compiler.c:error()`.
 */
noreturn void
lulu_Parse_error_consumed(lulu_VM *vm, lulu_Parser *parser, cstring msg);

#endif // LULU_PARSER_H
