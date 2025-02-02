#ifndef LULU_PARSER_H
#define LULU_PARSER_H

#include "compiler.h"
#include "lexer.h"

typedef enum {
    LVALUE_GLOBAL,
    LVALUE_LOCAL,
    LVALUE_TABLE,
} LValue_Type;

#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wgnu-anonymous-struct"
    #pragma GCC diagnostic ignored "-Wnested-anon-types"
#elif defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable : 4201)
#endif

struct LValue {
    LValue     *prev; // Use recursion to chain multiple assignments.
    LValue_Type type; // Determines what opcode we will use.
    union {
        struct { u16 i_table, i_key, n_pop; };
        u32 global;
        u8  local;
    }; // Arguments to the various opcodes.
};

#if defined(__GNUC__)
    #pragma GCC diagnostic pop
#elif defined(_MSC_VER)
    #pragma warning(pop)
#endif

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
} Precedence;

typedef void
(*Parse_Fn)(Parser *parser, Compiler *compiler);

typedef const struct {
    Parse_Fn   prefix_fn;
    Parse_Fn   infix_fn;
    Precedence precedence;
} Parse_Rule;

void
parser_init(lulu_VM *vm, Parser *self, Compiler *compiler, cstring filename, const char *input, isize len);

/**
 * @note 2024-09-06
 *      Analogous to the book's `compiler.c:advance()`.
 */
void
parser_advance_token(Parser *self);

/**
 * @note 2024-09-07
 *      Analogous to the book's `compiler.c:consume()`.
 */
void
parser_consume_token(Parser *self, Token_Type type, cstring msg);

/**
 * @brief
 *      If the current (a.k.a. 'lookahead') token matches, consume it.
 *      Otherwise, do nothing.
 */
bool
parser_match_token(Parser *self, Token_Type type);

/**
 * @note 2024-12-10
 *      Analogous to `compiler.c:declaration()` in the book.
 */
void
parser_declaration(Parser *self, Compiler *compiler);

/**
 * @note 2024-09-07
 *      Analogous to the book's `compiler.c:errorCurrent()`.
 */
LULU_ATTR_NORETURN
void
parser_error_current(Parser *self, cstring msg);

/**
 * @note 2024-09-07
 *      Analogous to the book's `compiler.c:error()`.
 */
LULU_ATTR_NORETURN
void
parser_error_consumed(Parser *self, cstring msg);

#endif // LULU_PARSER_H
