#ifndef LULU_LEXER_H
#define LULU_LEXER_H

#include "lulu.h"

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
    TK_NUMBER, // ` -?(0x[0-9a-fA-F]+|[0-9]+(\.|e)[0-9]+) ` := number literal
    TK_ERROR, 
    TK_EOF,
} TkType;

#define NUM_KEYWORDS    (TK_WHILE + 1)

typedef struct {
    const char *start;
    int len;
    int line;
    TkType type;
} Token;

typedef struct {
    const char *lexeme;   // Start of first
    const char *position; // Current character in source code.
    const char *name;     // Current filename or `"stdin"`.
    int line;             // Current line number we're on.
} Lexer;

void init_lexer(Lexer *self, const char *input, const char *name);

Token scan_token(Lexer *self);

#endif /* LULU_LEXER_H */
