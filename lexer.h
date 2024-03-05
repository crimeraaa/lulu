#ifndef LUA_LEXER_H
#define LUA_LEXER_H

#include "common.h"

/* Adapted from: https://www.lua.org/manual/5.1/manual.html */
typedef enum {
    // Single character tokens
    TK_LPAREN,   // '('  Grouping/function call/parameters begin.
    TK_RPAREN,   // ')'  Grouping/function call/parameters end.
    TK_LBRACE,   // '{'  Table literal begin.
    TK_RBRACE,   // '}'  Table literal end.
    TK_LBRACKET, // '['  Table indexing begin.
    TK_RBRACKET, // ']'  Table indexing end.
    TK_COMMA,    // ','  Function argument separator, multi-variable assignment.
    TK_PERIOD,   // '.'  Table field resolution.
    TK_COLON,    // ':'  Function method resolution (passes implicit `self`).
    TK_POUND,    // '#'  Get length of a table's array portion.
    TK_SEMICOL,  // ';'  Optional C-style statement separator.
    TK_ASSIGN,   // '='  Variable assignment.

    // Arithmetic Operators
    TK_PLUS,     // '+'  Addition.
    TK_DASH,     // '-'  Subtraction or unary negation, or a Lua comment.
    TK_STAR,     // '*'  Multiplication.
    TK_SLASH,    // '/'  Division.
    TK_CARET,    // '^'  Exponentiation.
    TK_PERCENT,  // '%'  Modulus, a.k.a. get the remainder.
                
    // Relational Operators
    TK_EQ,       // '==' Compare for equality.
    TK_NEQ,      // '~=' Compare for inequality.
    TK_GT,       // '>'  Greater than.
    TK_GE,       // '>=' Greater than or equal to.
    TK_LT,       // '<'  Less than.
    TK_LE,       // '<=' Less than or equal to.

    // Literals
    TK_FALSE,    // 'false'
    TK_IDENT,    // Variable name/identifier in source code, not a literal.
    TK_NIL,      // 'nil'
    TK_NUMBER,   // Number literal in integral/fractional/exponential form.
    TK_STRING,   // String literal, surrounded by balanced double/single quotes.
    TK_TABLE,    // Table literal, surrounded by balanced braces: '{' '}'.
    TK_TRUE,     // 'true'

    // Keywords
    TK_AND,
    TK_BREAK,
    TK_DO,       // 'do' Block delim in 'for', 'while'. Must be followed by 'end'. 
    TK_ELSE,
    TK_ELSEIF,
    TK_END,      // 'end'  Block delimiter for functions and control flow statements.
    TK_FOR,
    TK_FUNCTION,
    TK_IF,       // 'if' Simple conditional. Must be followed by 'then'.
    TK_IN,       // 'in' used by ipairs, pairs, and other stateless iterators.
    TK_LOCAL,    // 'local' declares a locally scoped variable.
    TK_NOT,
    TK_OR,
    TK_RETURN,   // 'return' Ends control flow and may push a value.
    TK_SELF,     // 'self' only a keyword for table methods using ':'.
    TK_THEN,     // 'then' Block delimiter for `if`.
    TK_WHILE,
    
    // Misc.
    TK_PRINT,    // Hack for now until we get builtin functions working.
    TK_CONCAT,   // '..' String concatenation.
    TK_VARARGS,  // '...' Function varargs.
    TK_ERROR,    // Distinct enumeration to allow us to detect actual errors.
    TK_EOF,      // EOF by itself is not an error.
    TK_COUNT,    // Determine size of the internal lookup table.
} TokenType;

typedef struct {
    TokenType type;
    const char *start; // Pointer to start of token in source code.
    size_t len;        // How many characters to dereference from `start`.
    int line;          // What line of the source code? Used for error reporting.
} Token;

/**
 * III:16.1.2   The scanner scans
 * 
 * The scanner, but I'll use the fancier term "lexer". 
 * This goes through your monolithic source code string. 
 * It carries state per lexeme.
 */
typedef struct {
    const char *start;   // Beginning of current lexeme in the source code.
    const char *current; // Current character in the source code.
    int line;            // Line number, for error reporting.
} Lexer;

void init_lexer(Lexer *self, const char *source);

/**
 * III:16.2.1   Scanning tokens
 * 
 * This is where the fun begins! Each call to this function scans a complete
 * token and gives you back said token for you to emit bytecode or determine
 * precedence of.
 * 
 * Remember that a token at this point does not have much syntactic purpose,
 * e.g. '(' could either be a function call or a grouping. We don't know yet.
 */
Token tokenize(Lexer *self);

#endif /* LUA_LEXER_H */
