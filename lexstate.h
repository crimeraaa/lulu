#ifndef LUA_LEXSTATE_H
#define LUA_LEXSTATE_H

#include <setjmp.h>
#include "common.h"

#define _tokenarray(...)            (toarraylit(TokenType, __VA_ARGS__))
#define _check_token(lex, types)    check_token_any(lex, types, arraylen(types))
#define _match_token(lex, types)    match_token_any(lex, types, arraylen(types))

/**
 * III:21.1.1   Print Statements
 * 
 * Check if the parser's CURRENT (not PREVIOUS) token matches `expected`.
 * 
 * III:23.2     If Statements
 * 
 * Now a wrapper macro in a similar style to `match_token()`.
 */
#define check_token(lex, ...)   _check_token(lex, _tokenarray(__VA_ARGS__))

/**
 * III:21.1.1   Print Statements
 * 
 * If token matches, consume it and return true. Otherwise return false. Nothing
 * more, nothing less. We don't throw the parser into a panic state.
 * 
 * 
 * III:23.2     If Statements
 * 
 * I've changed this to be a wrapper macro around the `_match_token()` macro
 * which is, in itself, a wrapper macro around `match_token_any()`.
 * 
 * This allows us to use the same "function" for 1 or more token types.
 */
#define match_token(lex, ...)   _match_token(lex, _tokenarray(__VA_ARGS__))

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
    TK_END,      // 'end' Block delimiter for functions and control flow statements.
    TK_FOR,
    TK_FUNCTION, // 'function' only ever used to define functions.
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
    const char *start; // Pointer to start of this token in source code.
    size_t len;        // How many characters to dereference from `start`?
    int line;          // What line of the source code? Used for error reporting.
} Token;

typedef struct {
    Token token; // Token we're pointing at and want to consume.
    Token consumed; // Token we just consumed.
    jmp_buf errjmp; // Where to unconditionally jump after reporting errors.
    const char *lexeme; // Pointer to start of current token in the code.
    const char *current;  // Pointer to current character being looked at.
    const char *name; // Filename or `stdin`.
    int linenumber; // Input line counter.
    int lastline;   // Line of the last token `consumed`.
    bool haderror;  // Track error state so we can report.
} LexState;

/**
 * @param self      A LexState instance we want to modify.
 * @param name      Filename string literal, or "stdin".
 * @param input     User's source code in one monolithic line.
 */
void init_lexstate(LexState *self, const char *name, const char *input);

/**
 * III:17.2     Parsing Tokens
 * 
 * Assume the compiler should move to the next token. So the LexState's parser 
 * half is set to start a new token.
 * 
 * III:24.5.4   Returning from functions
 * 
 * I'm doing a massive refactor to mimic the Lua C API's `LexState`. This
 * function replaces the old `advance_parser()` function.
 */
void next_token(LexState *self);

/**
 * III:17.2     Parsing Tokens
 * 
 * We only advance the LexState's parser if the current token matches the 
 * expected one. Otherwise, we set it into an error state and throw the error
 * using `longjmp`. Hope you used `setjmp` correctly!
 */
void consume_token(LexState *self, TokenType expected, const char *info);

/**
 * III:23.2     If Statements
 * 
 * Custom helper to determine if LexState's parser's current token matches any 
 * of the given ones.
 * 
 * @param parser    Parser instance.
 * @param expected  1D array literal of TokenTypes to check against.
 * @param count     Number of elements in `expected`, use the `arraylen` macro.
 * 
 * @return  `true` on the first match, else `false` if no match.
 * 
 * @note    This does not consume the token.
 */
bool check_token_any(LexState *self, TokenType *expected, size_t count);

/**
 * III:23.2     If Statements
 * 
 * Similar to `check_token_any` but we advance the parser if `true` as well.
 * Otherwise we return `false` without modifying any state.
 */
bool match_token_any(LexState *self, TokenType *expected, size_t count);

/**
 * III:17.2.1   Handling syntax errors
 * 
 * This is generic function to report errors based on some token and a message.
 * Whatever the case, we set the parser's error state to true.
 * 
 * III:23.3     While Statements
 * 
 * Assuming the `self->errjmp` was set up correctly, report an error and then
 * do a `longjmp()`. Ensure that this is never called in functions that require
 * cleanup like variable-length-arrays or heap-allocations.
 * 
 * III:24.5.4   Returning from functions
 * 
 * This function now replaaces `parser_error_at`, which itself replaced the
 * clox `errorAt()`.
 */
void throw_lexerror_at(LexState *self, const Token *token, const char *info);

/**
 * III:17.2.1   Handling syntax errors
 * 
 * More often than not, we want to report an error at the location of the token
 * we just consumed (that is, it's now the parser's previous token).
 * 
 * III:24.5.4   Returning from functions
 * 
 * This function now replaces `parser_error`, which itself originally replaced
 * the clox `error()` to throw errors at the current token.
 */
void throw_lexerror(LexState *self, const char *info);

#endif /* LUA_LEXSTATE_H */
