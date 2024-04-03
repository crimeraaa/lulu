#ifndef LUA_LEXICAL_ANALYZER_H
#define LUA_LEXICAL_ANALYZER_H

#include "lua.h"
#include "object.h"

typedef enum {
    // Reserved words (includes some value literals)
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
    TK_ERROR,   // Indicate to `Lexer` or `Compiler` to report the error.
    TK_EOF,     // EOF by itself is not an error.
} TkType;

// Maximum length of a reserved word.
#define TOKEN_LEN       arraylen("function")

// Reserved words must always come first in the enum definition.
#define NUM_RESERVED    (TK_WHILE + 1)

typedef struct Token Token;

/**
 * @brief   Instead of `Scanner` we have a `Lexer` which does multiple jobs.
 *          It primarily turns source code into a stream of tokens.
 *
 * @details Since we are on a single pass compiler you can imagine this is
 *          'buffered', in a way.
 *
 *          The `Compiler` asks for a token or two to determine the proper
 *          expression or operation type. Then it asks for tokens again, etc.
 */
typedef struct Lexer Lexer;

/**
 * @brief   Forward declared so we can use opaque pointers in declarations.
 *          Conceptually similar to Lua's `FuncState` as it actually handles 
 *          parsing, determining operator precedence and type, things like that.
 */
typedef struct Compiler Compiler;

struct Token {
    TkType type;
    const char *start;
    int len;  // How many characters to dereference from `start`.
    int line;
};

struct Lexer {
    Token token;          // Current token, considered 'consumed'.
    Token lookahead;      // Peek at the next token to be consumed.
    Compiler *func;       // Private to the parser's implementation file.
    const char *name;     // Filename of script, or `"stdin"` if in REPL. 
    const char *lexeme;   // Pointer to first character of current lexeme.
    const char *position; // Pointer to current character being looked at.
    int linenumber;       // Input line counter.
    int lastline;         // line number of last token 'consumed'.
};

void init_lex(Lexer *self, Compiler *compiler, const char *name, const char *input);
Token scan_token(Lexer *self);

// Update the lookahead token, only checking if we got an error.
void next_token(Lexer *self);

// Consume the lookahead token and advance if it matches else throw an error.
void consume_token(Lexer *self, TkType expected, const char *info);

// Report an error at the location of the token that was just consumed.
// This is functionally similar to the book's `errorAt()` function.
void lexerror_consumed(Lexer *self, const char *info);

// Report an error at the locatino of the token we are peeking at.
// e.g. the parser hands us an error token so we need to tell the user.
// Despite its name this functions similarly to the book's `errorAtCurrent()`.
void lexerror_lookahead(Lexer *self, const char *info);

void lexerror_at(Lexer *self, const Token *token, const char *info);

#endif /* LUA_LEXICAL_ANALYZER_H */
