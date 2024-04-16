#ifndef LULU_LEXER_H
#define LULU_LEXER_H

#include "lulu.h"
#include "limits.h"

typedef enum {
    // -*- RESERVED WORDS ------------------------------------------------- {{{1

    TK_AND,
    TK_BREAK,
    TK_DO,
    TK_ELSE,    TK_ELSEIF,  TK_END,
    TK_FALSE,   TK_FOR,     TK_FUNCTION,
    TK_IF,      TK_IN,
    TK_LOCAL,
    TK_NIL,     TK_NOT,
    TK_OR,
    TK_PRINT,   // This is temporary!
    TK_RETURN,
    TK_THEN,    TK_TRUE,
    TK_WHILE,

    // 1}}} --------------------------------------------------------------------

    // -*- SINGLE-CHARACTER TOKENS ---------------------------------------- {{{1

    TK_LPAREN,   TK_RPAREN,   // `(`, `)` := function calls, groupings
    TK_LBRACKET, TK_RBRACKET, // `[`, `]` := table index/field access
    TK_LCURLY,   TK_RCURLY,   // `{`, `}` := table constructors

    TK_COMMA,   // `,` := list separator (parameters, arguments or assignments)
    TK_SEMICOL, // `;` := optional statement ending
    TK_VARARG,  // `...` := variadic argument list, only useable in functions
    TK_CONCAT,  // `..`  := string concatenation
    TK_PERIOD,  // `.` := table field access, separate from `..` and `...`

    TK_PLUS,    // `+` := addition
    TK_DASH,    // `-` := subtraction, Lua-style comment (single or multi-line)
    TK_STAR,    // `*` := multiplication
    TK_SLASH,   // `/` := division
    TK_PERCENT, // `%` := modulo
    TK_CARET,   // `^` := exponentiation

    // 1}}} --------------------------------------------------------------------

    // -*- MULTI-CHARACTER TOKENS ----------------------------------------- {{{1

    TK_ASSIGN,     // `=` := variable assignment
    TK_EQ, TK_NEQ, // `==`, `~=` := equal to,     not equal to
    TK_GT, TK_GE,  // `>`,  `>=` := greater than, greater-than-or-equal-to
    TK_LT, TK_LE,  // `<`,  `<=` := less than,    greater-than-or-equal-to

    // 1}}} --------------------------------------------------------------------

    TK_IDENT,  // ` [a-zA-Z_][a-zA-Z0-9_]+ ` := variable name/identifier
    TK_STRING, // ` (".*"|'.*') ` := string literal
    TK_NUMBER, // ` [0-9]+(\.[0-9]+)? `, ` 0x[a-fA-F0-9]+ ` := number literal
    TK_ERROR,
    TK_EOF,
} TkType;

#define NUM_KEYWORDS    (TK_WHILE + 1)
#define NUM_TOKENS      (TK_EOF + 1)

typedef struct {
    const char *start;
    int len;
    int line;
    TkType type;
} Token;

typedef struct {
    Token token;          // analogous to `Parser::current` in the book.
    Token consumed;       // analogous to `Parser::previous` in the book.
    struct VM *vm;        // Private to implementation. Has our `jmp_buf`.
    const char *lexeme;   // Pointer to first character of the current lexeme.
    const char *position; // Current character in source code.
    const char *name;     // Current filename or `"stdin"`.
    int line;             // Current line number we're on.
} Lexer;

void init_lexer(Lexer *self, const char *input, struct VM *vm);

Token scan_token(Lexer *self);

// Analogous to `compiler.c:advance()` in the book. May call `lexerror_*`.
void next_token(Lexer *self);

// Analogous to `compiler.c:consume()` in the book. May call `lexerror_*`.
void consume_token(Lexer *self, TkType expected, const char *info);

bool check_token(Lexer *self, const TkType expected[]);
bool match_token(Lexer *self, const TkType expected[]);

#define _token_vargs(...)       array_lit(TkType, __VA_ARGS__, TK_EOF)
#define check_token(lexer, ...) check_token(lexer, _token_vargs(__VA_ARGS__))
#define match_token(lexer, ...) match_token(lexer, _token_vargs(__VA_ARGS__))

/**
 * @brief   Similar to `luaX_lexerror()`, analogout to `errorAt()` in the book.
 *
 * @warning Calls `longjmp`. If used incorrectly you may leak memory, corrupt
 *          the C stack, or just cause undefined behavior all around. By all
 *          means DO NOT call `longjmp` at the same callsite/stackframe twice!
 */
void lexerror_at(Lexer *self, const Token *token, const char *info);

// Similar to `luaX_syntaxerror()`, analogous to `errorAtCurrent()` in the book.
void lexerror_at_token(Lexer *self, const char *info);

// Analogous to `error()` in the book.
void lexerror_at_consumed(Lexer *self, const char *info);

#endif /* LULU_LEXER_H */
