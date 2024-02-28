#include "compiler.h"
#include "lexer.h"
#include "parser.h"

void init_parser(Parser *self, const char *source) {
    self->haderror  = false;
    self->panicking = false;
    init_lexer(&self->lexer, source);
}

void parser_error_at(Parser *self, const Token *token, const char *message) {
    if (self->panicking) {
        return; // Avoid cascading errors for user's sanity
    }
    self->haderror = true;
    self->panicking = true;
    fprintf(stderr, "[line %i] Error", token->line);
    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // Nothing as the error token already has a message.
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }
    fprintf(stderr, ": %s\n", message);
}

void parser_error(Parser *self, const char *message) {
    parser_error_at(self, &self->previous, message);
}

/**
 * III:17.2.1   Handling syntax errors
 * 
 * If the lexer hands us an error token, we simply report its error message.
 * This is a wrapper around the more generic `parser_error_at()` which can take in any
 * arbitrary error message.
 */
static inline void parser_error_at_current(Parser *self, const char *message) {
    parser_error_at(self, &self->current, message);
}

void advance_parser(Parser *self) {
    self->previous = self->current;
    
    for (;;) {
        self->current = tokenize(&self->lexer);
        if (self->current.type != TOKEN_ERROR) {
            break;
        }
        // Error tokens already point to an error message thanks to the lexer.
        parser_error_at_current(self, self->current.start);
    }
}

void consume_token(Parser *self, TokenType expected, const char *message) {
    if (self->current.type == expected) {
        advance_parser(self);
        return;
    }
    parser_error_at_current(self, message);
}

bool match_token(Parser *self, TokenType expected) {
    if (!check_token(self, expected)) {
        return false;
    }
    advance_parser(self);
    return true;
}

bool check_token(const Parser *self, TokenType expected) {
    return self->current.type == expected;
}

void synchronize_parser(Parser *self) {
    self->panicking = false;
    
    while (self->current.type != TOKEN_EOF) {
        if (self->previous.type == TOKEN_SEMICOL) {
            // If more than 1 semicol, don't report the error once since we
            // reset the parser panic state.
            if (self->current.type != TOKEN_SEMICOL) {
                return;
            }
        }
        switch (self->current.type) {
        case TOKEN_FUNCTION:
        case TOKEN_LOCAL:
        case TOKEN_FOR:
        case TOKEN_IF:
        case TOKEN_WHILE:
        case TOKEN_PRINT:
        case TOKEN_RETURN: return;
        default:
            ; // Do nothing
        }
        // Consume token without doing anything with it
        advance_parser(self);
    }
}
