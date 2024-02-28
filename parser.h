#ifndef LUA_PARSER_H
#define LUA_PARSER_H

#include "common.h"
#include "lexer.h"

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
 * III:21.1.1   Print Statements
 * 
 * Check if the parser's CURRENT (not PREVIOUS) token matches `expected`.
 */
bool check_token(const Parser *self, TokenType expected);

/**
 * III:21.1.1   Print Statements
 * 
 * If token matches, consume it and return true. Otherwise return false. Nothing
 * more, nothing less. We don't throw the parser into a panic state.
 */
bool match_token(Parser *self, TokenType expected);

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
