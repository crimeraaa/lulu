#ifndef LULU_PARSER_H
#define LULU_PARSER_H

#include "compiler.h"
#include "lexer.h"

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
(*lulu_ParseFn)(lulu_Parser *parser, lulu_Lexer *lexer, lulu_Compiler *compiler);

typedef const struct {
    lulu_ParseFn    prefix_fn;
    lulu_ParseFn    infix_fn;
    lulu_Precedence precedence;
} lulu_Parser_Rule;

/**
 * @note 2024-09-06
 *      Analogous to the book's `compiler.c:advance()`.
 */
void
lulu_Parser_advance_token(lulu_Parser *self, lulu_Lexer *lexer);

/**
 * @note 2024-09-07
 *      Analogous to the book's `compiler.c:consume()`.
 */
void
lulu_Parser_consume_token(lulu_Parser *self, lulu_Lexer *lexer, lulu_Token_Type type, cstring msg);

/**
 * @brief
 *      If the current (a.k.a. 'lookahead') token matches, consume it.
 *      Otherwise, do nothing.
 */
bool
lulu_Parser_match_token(lulu_Parser *self, lulu_Lexer *lexer, lulu_Token_Type type);

void
lulu_Parser_declaration(lulu_Parser *self, lulu_Lexer *lexer, lulu_Compiler *compiler);

/**
 * @note 2024-09-07
 *      Analogous to the book's `compiler.c:errorCurrent()`.
 */
LULU_ATTR_NORETURN
void
lulu_Parser_error_current(lulu_Parser *self, lulu_Lexer *lexer, cstring msg);

/**
 * @note 2024-09-07
 *      Analogous to the book's `compiler.c:error()`.
 */
LULU_ATTR_NORETURN
void
lulu_Parser_error_consumed(lulu_Parser *self, lulu_Lexer *lexer, cstring msg);

#endif // LULU_PARSER_H
