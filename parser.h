#ifndef LUA_PARSER_H
#define LUA_PARSER_H

#include "common.h"
#include "lexer.h"

#define _tokenarray(...)            (toarraylit(TokenType, __VA_ARGS__))
#define _check_token(parser, types) check_token_any(parser, types, arraylen(types))
#define _match_token(parser, types) match_token_any(parser, types, arraylen(types))

/**
 * III:21.1.1   Print Statements
 * 
 * Check if the parser's CURRENT (not PREVIOUS) token matches `expected`.
 * 
 * III:23.2     If Statements
 * 
 * Now a wrapper macro in a similar style to `match_token()`.
 */
#define check_token(parser, ...) _check_token(parser, _tokenarray(__VA_ARGS__))

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
#define match_token(parser, ...) _match_token(parser, _tokenarray(__VA_ARGS__))

/**
 * III:17.2     Parsing Tokens
 * 
 * This struct works in tandem with `Lexer`. The Lexer emits raw tokens, and
 * the Parser keeps track of which token we have right now and which token we just
 * had. This kind of lookahead (or lookbehind) is just enough to parse complex
 * expressions.
 */
typedef struct {
    Lexer lexer;    // Tokenizer and lookahead.
    Token current;  // Token we're pointing at and want to consume.
    Token previous; // Token we just consumed.
    bool haderror;  // Track error state so we can report.
    bool panicking; // Track panic state so we don't vomit error cascades.
} Parser;

/**
 * Start the parser with some source code.
 */
void init_parser(Parser *self, const char *source);

/**
 * III:17.2.1   Handling syntax errors
 * 
 * This is generic function to report errors based on some token and a message.
 * Whatever the case, we set the parser's error state to true.
 */
void parser_error_at(Parser *self, const Token *token, const char *message);

/**
 * III:17.2.1   Handling syntax errors
 * 
 * More often than not, we want to report an error at the location of the token
 * we just consumed (that is, it's now the parser's previous token).
 */
void parser_error(Parser *self, const char *message);

/**
 * III:17.2     Parsing Tokens
 * 
 * Assume the compiler should move to the next token. So the parser is set to
 * start a new token. This adjusts state of the compiler's parser and lexer.
 */
void advance_parser(Parser *self);

/**
 * III:17.2     Parsing Tokens
 * 
 * We only advance the compiler if the current token matches the expected one.
 * Otherwise, we set the compiler into an error state and report the error.
 */
void consume_token(Parser *self, TokenType expected, const char *message);

/**
 * III:23.2     If Statements
 * 
 * Custom helper to determine if parser's current token matches any of the given.
 * 
 * @param parser    Parser instance.
 * @param expected  1D array literal of TokenTypes to check against.
 * @param count     Number of elements in `expected`, use the `arraylen` macro.
 * 
 * @return          `true` on the first match, else `false` if no match.
 * 
 * @note            This does not consume the token.
 */
bool check_token_any(const Parser *parser, const TokenType *expected, size_t count);

/**
 * III:23.2     If Statements
 * 
 * Similar to `check_token_any` but we advance the parser if `true` as well.
 * Otherwise we return `false` without modifying any state.
 */
bool match_token_any(Parser *self, const TokenType *expected, size_t count);

/**
 * III:21.1.3   Error synchronization
 * 
 * If we hit a compile error while parsing a previous statement we panic.
 * When we panic, we attempt to synchronize by moving the parser to the next
 * statement boundary. A statement boundary is a preceding token that can end
 * a statement, like a semicolon. Or a subsequent token that begins a statement,
 * like a control flow or declaration statement.
 */
void synchronize_parser(Parser *parser);

#endif /* LUA_PARSER_H */
