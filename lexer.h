#ifndef LUA_LEXER_H
#define LUA_LEXER_H

#include "common.h"

/* Adapted from: https://www.lua.org/manual/5.1/manual.html */
typedef enum {
    // Single character tokens
    TOKEN_LPAREN,   // '('  Grouping/function call/parameters begin.
    TOKEN_RPAREN,   // ')'  Grouping/function call/parameters end.
    TOKEN_LBRACE,   // '{'  Table literal begin.
    TOKEN_RBRACE,   // '}'  Table literal end.
    TOKEN_LBRACKET, // '['  Table indexing begin.
    TOKEN_RBRACKET, // ']'  Table indexing end.
    TOKEN_COMMA,    // ','  Function argument separator, multi-variable assignment.
    TOKEN_PERIOD,   // '.'  Table field resolution.
    TOKEN_COLON,    // ':'  Function method resolution (passes implicit `self`).
    TOKEN_POUND,    // '#'  Get length of a table's array portion.
    TOKEN_SEMICOL,  // ';'  Optional C-style statement separator.
    TOKEN_EQUAL,    // '='  Variable assignment.

    // Arithmetic Operators
    TOKEN_PLUS,     // '+'  Addition.
    TOKEN_DASH,     // '-'  Subtraction or unary negation, or a Lua comment.
    TOKEN_STAR,     // '*'  Multiplication.
    TOKEN_SLASH,    // '/'  Division.
    TOKEN_CARET,    // '^'  Exponentiation.
    TOKEN_PERCENT,  // '%'  Modulus, a.k.a. get the remainder.
                
    // Relational Operators
    TOKEN_REL_EQ,   // '==' Compare for equality.
    TOKEN_REL_NEQ,  // '~=' Compare for inequality.
    TOKEN_REL_GT,   // '>'  Greater than.
    TOKEN_REL_GE,   // '>=' Greater than or equal to.
    TOKEN_REL_LT,   // '<'  Less than.
    TOKEN_REL_LE,   // '<=' Less than or equal to.

    // Literals
    TOKEN_FALSE,    // 'false'
    TOKEN_IDENT,    // Variable name/identifier in source code, not a literal.
    TOKEN_NIL,      // 'nil'
    TOKEN_NUMBER,   // Number literal in integral/fractional/exponential form.
    TOKEN_STRING,   // String literal, surrounded by balanced double/single quotes.
    TOKEN_TABLE,    // Table literal, surrounded by balanced braces: '{' '}'.
    TOKEN_TRUE,     // 'true'

    // Keywords
    TOKEN_AND,
    TOKEN_BREAK,
    TOKEN_DO,       // 'do' Block delim in 'for', 'while'. Must be followed by 'end'. 
    TOKEN_ELSE,
    TOKEN_ELSEIF,
    TOKEN_END,      // 'end'  Block delimiter for functions and control flow statements.
    TOKEN_FOR,
    TOKEN_FUNCTION,
    TOKEN_IF,       // 'if' Simple conditional. Must be followed by 'then'.
    TOKEN_IN,       // 'in' used by ipairs, pairs, and other stateless iterators.
    TOKEN_LOCAL,    // 'local' declares a locally scoped variable.
    TOKEN_NOT,
    TOKEN_OR,
    TOKEN_RETURN,   // 'return' Ends control flow and may push a value.
    TOKEN_SELF,     // 'self' only a keyword for table methods using ':'.
    TOKEN_THEN,     // 'then' Block delimiter for `if`.
    TOKEN_WHILE,
    
    // Misc.
    TOKEN_CONCAT,   // '..' String concatenation.
    TOKEN_VARARGS,  // '...' Function varargs.
    TOKEN_ERROR,    // Distinct enumeration to allow us to detect actual errors.
    TOKEN_EOF,      // EOF by itself is not an error.
    TOKEN_COUNT,    // Determine size of the internal lookup table.
} LuaTokenType;

typedef struct {
    LuaTokenType type;
    const char *start; // Pointer to start of token in source code.
    int length; // How many characters to dereference from `start`.
    int line;   // What line of the source code? Used for error reporting.
} LuaToken;

/**
 * III:16.1.2   The scanner scans
 * 
 * The scanner, but I'll use the fancier term "lexer", goes through your monolithic
 * source code string. It carries state per lexeme.
 */
typedef struct {
    const char *start;   // Beginning of current lexeme in the source code.
    const char *current; // Current character in the source code.
    int line;            // Line number, for error reporting.
} LuaLexer;

void init_lexer(LuaLexer *self, const char *source);

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
LuaToken tokenize(LuaLexer *self);

#endif /* LUA_LEXER_H */
