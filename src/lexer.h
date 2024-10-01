#ifndef LULU_LEXER_H
#define LULU_LEXER_H

#include "string.h"

/**
 * @link
 *      https://github.com/crimeraaa/lulu/blob/main/.archive/2024-09-04/lexer.h
 */
typedef enum {
///=== RESERVED WORDS ==========================================================

TOKEN_AND,
TOKEN_BREAK,
TOKEN_DO,
TOKEN_ELSE,    TOKEN_ELSEIF,  TOKEN_END,
TOKEN_FALSE,   TOKEN_FOR,     TOKEN_FUNCTION,
TOKEN_IF,      TOKEN_IN,
TOKEN_LOCAL,
TOKEN_NIL,     TOKEN_NOT,
TOKEN_OR,
TOKEN_PRINT,   // This is temporary!!!
TOKEN_REPEAT,  TOKEN_RETURN,
TOKEN_THEN,    TOKEN_TRUE,
TOKEN_UNTIL,
TOKEN_WHILE,

///=============================================================================

///=== SINGLE=CHARACTER TOKENS =================================================

TOKEN_PAREN_L,   TOKEN_PAREN_R,   // ( ) := function calls, groupings
TOKEN_BRACKET_L, TOKEN_BRACKET_R, // [ ] := table index/field access
TOKEN_CURLY_L,   TOKEN_CURLY_R,   // { } := table constructors

TOKEN_COMMA,        // ,   := list separator (parameters/arguments, assignments)
TOKEN_COLON,        // :   := syntactic sugar to pass calling table as first parameter
TOKEN_SEMICOLON,    // ;   := optional statement ending
TOKEN_ELLIPSIS_3,   // ... := variadic argument list, only useable in functions
TOKEN_ELLIPSIS_2,   // ..  := string concatenation
TOKEN_PERIOD,       // .   := table field access, separate from .. and ...
TOKEN_HASH,         // #   := table/string length unary operator.

TOKEN_PLUS,         // + := addition
TOKEN_DASH,         // - := subtraction, Lua-style comment (single/multi-line)
TOKEN_STAR,         // * := multiplication
TOKEN_SLASH,        // / := division
TOKEN_PERCENT,      // % := modulo
TOKEN_CARET,        // ^ := exponentiation

///=============================================================================

///=== MULTI=CHARACTER TOKENS ==================================================

TOKEN_EQUAL,
TOKEN_EQUAL_EQUAL, TOKEN_TILDE_EQUAL,   // == ~=
TOKEN_ANGLE_L,     TOKEN_ANGLE_L_EQUAL, // <  <=
TOKEN_ANGLE_R,     TOKEN_ANGLE_R_EQUAL, // >  >=

///=============================================================================

TOKEN_IDENTIFIER, //  [a-zA-Z_][a-zA-Z0-9_]+  := variable name/identifier
TOKEN_STRING_LIT, //  ".*" or '.*'  := string literal
TOKEN_NUMBER_LIT, //  [0-9]+(\.[0-9]+)? or  0x[a-fA-F0-9]+  := number literal
TOKEN_ERROR,
TOKEN_EOF,
} lulu_Token_Type;

#define LULU_KEYWORD_COUNT  (TOKEN_WHILE + 1)
#define LULU_TOKEN_COUNT    (TOKEN_EOF + 1)

typedef struct {
    const char     *start;
    isize           len;
    lulu_Token_Type type;  // Placed as 3rd member due to padding of above.
    int             line;
} lulu_Token;

typedef struct {
    lulu_VM     *vm;       // Pointer to parent/enclosing state. Has allocator.
    cstring      filename; // Name of current file being lexed.
    lulu_String *string;   // Interned string literal if we currently have one.
    lulu_Number  number;   // Number literal if we currently have one.
    const char  *start;
    const char  *current;
    int          line;
} lulu_Lexer;

typedef struct {
    const char *data;
    isize       len;
} Char_Slice;

/**
 * @brief
 *      Map a `lulu_Token_Type`, to the string representation thereof.
 */
extern const Char_Slice
LULU_TOKEN_STRINGS[LULU_TOKEN_COUNT];

void
lulu_Lexer_init(lulu_VM *vm, lulu_Lexer *self, cstring filename, cstring input);

lulu_Token
lulu_Lexer_scan_token(lulu_Lexer *self);

#endif // LULU_LEXER_H
