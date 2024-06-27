#ifndef LULU_LEXER_H
#define LULU_LEXER_H

#include "lulu.h"
#include "limits.h"
#include "object.h"
#include "zio.h"

typedef enum {
    // --- RESERVED WORDS ------------------------------------------------- {{{1

    TK_AND,
    TK_BREAK,
    TK_DO,
    TK_ELSE,    TK_ELSEIF,  TK_END,
    TK_FALSE,   TK_FOR,     TK_FUNCTION,
    TK_IF,      TK_IN,
    TK_LOCAL,
    TK_NIL,     TK_NOT,
    TK_OR,
    TK_PRINT,   // This is temporary!!!
    TK_RETURN,
    TK_THEN,    TK_TRUE,
    TK_WHILE,

    // 1}}} --------------------------------------------------------------------

    // --- SINGLE-CHARACTER TOKENS ---------------------------------------- {{{1

    TK_LPAREN,   TK_RPAREN,   // `(`, `)` := function calls, groupings
    TK_LBRACKET, TK_RBRACKET, // `[`, `]` := table index/field access
    TK_LCURLY,   TK_RCURLY,   // `{`, `}` := table constructors

    TK_COMMA,   // `,` := list separator (parameters, arguments or assignments)
    TK_SEMICOL, // `;` := optional statement ending
    TK_VARARG,  // `...` := variadic argument list, only useable in functions
    TK_CONCAT,  // `..`  := string concatenation
    TK_PERIOD,  // `.` := table field access, separate from `..` and `...`
    TK_POUND,   // `#` := table/string length unary operator.

    TK_PLUS,    // `+` := addition
    TK_DASH,    // `-` := subtraction, Lua-style comment (single or multi-line)
    TK_STAR,    // `*` := multiplication
    TK_SLASH,   // `/` := division
    TK_PERCENT, // `%` := modulo
    TK_CARET,   // `^` := exponentiation

    // 1}}} --------------------------------------------------------------------

    // --- MULTI-CHARACTER TOKENS ----------------------------------------- {{{1

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

typedef union {
    String *string; // Used by IDENT, STRING, ERROR and potentially NUMBER.
    Number  number; // Used by NUMBER when conversion is successful.
} TkData;

typedef struct {
    TkData data;
    TkType type;
    int    line;
} Token;

typedef struct {
    Stream  *stream;
    Buffer  *buffer;    // Buffer to store string representation of some tokens.
    Token    lookahead; // analogous to `Parser::current` in the book.
    Token    consumed;  // analogous to `Parser::previous` in the book.
    lulu_VM *vm;        // Private to implementation. Has our `jmp_buf`.
    int      line;      // Current line number we're on.
    char     current;   // Currently character stream is pointing to.
} Lexer;

void luluLex_intern_tokens(lulu_VM *vm);

void  luluLex_init(lulu_VM *vm, Lexer *ls, Stream *z, Buffer *b);
Token luluLex_scan_token(Lexer *ls);

// Analogous to `compiler.c:advance()` in the book. May call `lexerror_*`.
void luluLex_next_token(Lexer *ls); // `compiler.c:advance()`

// Analogous to `compiler.c:consume()` in the book. Throws error if no match.
// If `info` is non-null we will append it to the error message.
void luluLex_expect_token(Lexer *ls, TkType type, const char *info);

// Return true if the current token matches, else do nothing.
bool luluLex_check_token(Lexer *ls, TkType type);

// Advance the Lexer if the current token matches else do nothing.
bool luluLex_match_token(Lexer *ls, TkType type);

/**
 * @brief   Similar to `luaX_lexerror()`, analogout to `errorAt()` in the book.
 *
 * @warning Calls `longjmp`. If used incorrectly you may leak memory, corrupt
 *          the C stack, or just cause undefined behavior all around. By all
 *          means DO NOT call `longjmp` at the same callsite/stackframe twice!
 */
void luluLex_error_at(Lexer *ls, const Token *tk, const char *info);

// Similar to `luaX_syntaxerror()`, analogous to `errorAtCurrent()` in the book.
void luluLex_error_lookahead(Lexer *ls, const char *info);

// Analogous to `error()` in the book.
void luluLex_error_consumed(Lexer *ls, const char *info);

// Only used when we need to error in the middle of consuming a token.
void luluLex_error_middle(Lexer *ls, const char *info);

#endif /* LULU_LEXER_H */
