#ifndef LUA_LEXICAL_ANALYZER_H
#define LUA_LEXICAL_ANALYZER_H

#include "lua.h"
#include "object.h"

typedef enum {
    // Reserved words (includes literals)
    TK_AND,
    TK_BREAK,
    TK_DO,
    TK_ELSE,  TK_ELSEIF, TK_END,
    TK_FALSE, TK_FOR,    TK_FUNCTION,
    TK_IF,    TK_IN,
    TK_LOCAL,
    TK_NIL,   TK_NOT,
    TK_OR,
    TK_RETURN,
    TK_THEN,  TK_TRUE,
    TK_WHILE,

    // Arithmetic operators
    TK_PLUS,        // `+` := addition
    TK_DASH,        // `-` := subtraction
    TK_STAR,        // `*` := multiplication
    TK_SLASH,       // `/` := division
    TK_PERCENT,     // `%` := modulus/remainder
    TK_CARET,       // `^` := exponentiation

    // Relational operators
    TK_EQ, TK_NEQ,  // `==`, `~=` := equality, inequality
    TK_GT, TK_GE,   //  `>`, `>=` := greater-than, greater-than-or-equal-to
    TK_LT, TK_LE,   //  `<`, `<=` := less-than, less-then-or-equal-to

    // Balanced pairs
    TK_LPAREN,   TK_RPAREN,     // `(`, `)` := function call, grouping
    TK_LBRACKET, TK_RBRACKET,   // `[`, `]` := table index/access
    TK_LCURLY,   TK_RCURLY,     // `{`, `}` := table constructor

    // Punctuation marks
    TK_ASSIGN,  // `=`   := Variable assignment.
    TK_COMMA,   // `,`   := parameter/argument list, multiple assignment, fields
    TK_SEMICOL, // `;`   := Optional statement ending, no more than 1 allowed.
    TK_PERIOD,  // `.`   := Table field access.
    TK_CONCAT,  // `..`  := String concatenation.
    TK_VARARG,  // `...` := Indicates a function needs variadic arguments.

    // Variably-sized tokens
    TK_NUMBER,  // [0-9]+        := Number literal.
    TK_NAME,    // [a-zA-Z0-9_]+ := Variable names/identifiers.
    TK_STRING,  // ".*"|'.*'     := Quote-enclosed string literal.

    // Misc.
    TK_ERROR,   // Indicate to `LexState` or `Compiler` to report the error.
    TK_EOF,     // EOF by itself is not an error.
} TkType;

// Maximum length of a reserved word.
#define TOKEN_LEN       arraylen("function")

// Reserved words must always come first in the enum definition.
#define NUM_RESERVED    cast(int, TK_WHILE + 1)

typedef struct Token Token;

// Combination of `Scanner` (a.k.a. `Lexer`) and `Parser` in the book.
// Performs lexical analysis.
typedef struct LexState LexState;

struct Token {
    TkType type;
    const char *start;
    int len;           // How many characters to dereference from `start`.
    int line;
};

struct LexState {
    const char *lexeme;  // Pointer to first character of current lexeme.
    const char *position; // Pointer to current character being looked at.
    int linenumber; // Input line counter.
    int lastline;   // line number of last token 'consumed'.
};

void init_lexstate(LexState *self, const char *input);
Token scan_token(LexState *self);

#endif /* LUA_LEXICAL_ANALYZER_H */
