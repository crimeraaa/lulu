#include <ctype.h>
#include "lexer.h"

void init_lexer(Lexer *self, const char *source) {
    self->start   = source;
    self->current = source;
    self->line    = 1;
}

/**
 * Variables cannot start with digits. Think: `[a-zA-Z_]`
 *
 * We use a function so that macros passed to this aren't expanded twice, which
 * can have potentially unwanted side-effects.
 */
static inline bool isidentstarter(char ch) {
    return isalpha(ch) || ch == '_';
}

/**
 * Past the first character, variable names can be in the regex:
 * `[a-zA-Z0-9_]`
 *
 * We use a function so that macros passed as arguments aren't expanded twice.
 * Those can have potentially unwanted side-effects.
 */
static inline bool isident(char ch) {
    return isalnum(ch) || ch == '_';
}

/**
 * Check if the current character pointer points to a nul char. If it does,
 * chances are we've reached the end of the monolithic string.
 */
static inline bool is_at_end(const Lexer *self) {
    return *self->current == '\0'; // precedence: member access > dereferencing
}

/**
 * Return the current character and then increments the `self->current` pointer.
 */
static inline char advance_lexer(Lexer *self) {
    return *(self->current++); // Paranoid about precedence here
}

/**
 * Return the lexer's current pointed-at character without adjusting any internal
 * state.
 */
static inline char peek_current(const Lexer *self) {
    return *self->current;
}

/**
 * Return the character right after the lexer's current character without adjusting
 * any internal state.
 */
static inline char peek_next(const Lexer *self) {
    return is_at_end(self) ? '\0' : *(self->current + 1);
}

/**
 * A bit of a strange name. If the lexer's current character matches `expected`,
 * the lexer's current character pointer is incremented and we return `true`.
 *
 * Otherwise, we return false without incrementing anything.
 */
static inline bool match_lexer(Lexer *self, char expected) {
    if (is_at_end(self) || *self->current != expected) {
        return false;
    }
    self->current++;
    return true;
}

/**
 * Create a new Token based on the lexer's current state.
 */
static Token make_token(const Lexer *self, TokenType type) {
    Token token;
    token.type   = type;
    token.start  = self->start;
    token.len    = self->current - self->start;
    token.line   = self->line;
    return token;
}

/**
 * Quickly create an error token that points to the given message.
 *
 * To be absolutely safe, please only pass read-only string literals to `message`.
 */
static Token error_token(const Lexer *self, const char *message) {
    Token token;
    token.type   = TOKEN_ERROR;
    token.start  = message;
    token.len    = strlen(message);
    token.line   = self->line;
    return token;
}

/**
 * Ignore whitespace characters.
 */
static void skip_whitespace(Lexer *self) {
    for (;;) {
        char ch = peek_current(self);
        switch (ch) {
        case ' ': // fall through
        case '\r':
        case '\t':
            advance_lexer(self);
            break;
        case '\n':
            self->line++;
            advance_lexer(self);
            break;
        case '-':
            // Singlular '-' indicate we don't have a Lua-style comment.
            if (peek_next(self) != '-') {
                return;
            }
            // Comments aren't whitespace but we may as well ignore them here.
            while (peek_current(self) != '\n' && !is_at_end(self)) {
                advance_lexer(self);
            }
            break;
        default: return;
        }
    }
}

static TokenType check_keyword(const Lexer *self,
    Size idx,
    Size len,
    const char *kw,
    TokenType type)
{
    // self->current points to the last character, we can get the lexeme's length.
    // Assumes that the current pointer is always a higher address than start.
    if ((Size)(self->current - self->start) == len) {
        // We only want to compare past the offset, save some cycles maybe.
        if (memcmp(self->start + idx, &kw[idx], (len - idx)) == 0) {
            return type;
        }
    }
    return TOKEN_IDENT;
}

/**
 * Wrapper around the above function so the me-facing call doesn't look so crazy.
 *
 * @param lexer     Pointer to a lexer instance.
 * @param idx     The offset index into `Word` where we begin the comparison.
 * @param kw   String literal of the kw in question.
 * @param token     Token type to be returned if lexer's substring matches `Word`.
 *
 * @note        We get the `sizeof(Word) - 1` because string literals are always
 *              going to be nul terminated, and we want only the non-nul length.
 */
#define check_keyword(lexer, idx, kw, token) \
    check_keyword(lexer, idx, sizeof(kw) - 1, kw, token)

static TokenType ident_type(Lexer *self) {
    // I'm assuming that current will always be a higher address than start.
    Size lexlen = self->current - self->start;
    const char *lexeme = self->start;
    switch (lexeme[0]) {
        case 'a': return check_keyword(self, 1, "and", TOKEN_AND);
        case 'y': return check_keyword(self, 1, "break", TOKEN_BREAK);
        case 'd': return check_keyword(self, 1, "do", TOKEN_DO);
        case 'e': {
            if (lexlen > 1) {
                switch(lexeme[1]) {
                case 'l':
                    // This is horrible
                    switch (lexlen) {
                    case 4: return check_keyword(self, 2, "else", TOKEN_ELSE);
                    case 6: return check_keyword(self, 2, "elseif", TOKEN_ELSEIF);
                    }
                    break;
                case 'n': return check_keyword(self, 2, "end", TOKEN_END);
                }
            }
            break;
        }
        case 'f': {
            if (lexlen > 1) {
                switch(lexeme[1]) {
                case 'a': return check_keyword(self, 2, "false", TOKEN_FALSE);
                case 'o': return check_keyword(self, 2, "for", TOKEN_FOR);
                case 'u': return check_keyword(self, 2, "function", TOKEN_FUNCTION);
                }
            }
            break;
        }
        case 'i': {
            if (lexlen > 1) {
                switch (lexeme[1]) {
                case 'f': return check_keyword(self, 2, "if", TOKEN_IF);
                case 'n': return check_keyword(self, 2, "in", TOKEN_IN);
                }
            }
            break;
        }
        case 'l': return check_keyword(self, 1, "local", TOKEN_LOCAL);
        case 'n': {
            if (lexlen > 1) {
                switch (lexeme[1]) {
                case 'i': return check_keyword(self, 2, "nil", TOKEN_NIL);
                case 'o': return check_keyword(self, 2, "not", TOKEN_NOT);
                }
            }
            break;
        }
        case 'o': return check_keyword(self, 1, "or", TOKEN_OR);
        // TODO: Hack, remove this when we have a builtin print function
        case 'p': return check_keyword(self, 1, "print", TOKEN_PRINT);
        case 'r': return check_keyword(self, 1, "return", TOKEN_RETURN);
        case 's': return check_keyword(self, 1, "self", TOKEN_SELF);
        case 't': {
            if (lexlen > 1) {
                switch (lexeme[1]) {
                case 'h': return check_keyword(self, 2, "then", TOKEN_THEN);
                case 'r': return check_keyword(self, 2, "true", TOKEN_TRUE);
                }
            }
            break;
        }
        case 'w': return check_keyword(self, 1, "while", TOKEN_WHILE);
    }
    return TOKEN_IDENT;
}

/**
 * Assumes we consumed an alphabetical/underscore character already.
 *
 * Past the first character, any keyword/identifier can contain alphabeticals,
 * numbers or underscores.
 */
static Token ident_token(Lexer *self) {
    while (isident(peek_current(self))) {
        advance_lexer(self);
    }
    return make_token(self, ident_type(self));
}

/**
 * Assumes we already consumed a digit that we believe to be the start of a
 * number literal. We try to consume the rest of it.
 *
 * Conversion will be handled by the parser.
 */
static Token number_token(Lexer *self) {
    while (isdigit(peek_current(self))) {
        advance_lexer(self);
    }
    // Look for a fractional part.
    if (peek_current(self) == '.' && isdigit(peek_next(self))) {
        // Consume the '.' character.
        advance_lexer(self);
        while (isdigit(peek_current(self))) {
            advance_lexer(self);
        }
    }
    return make_token(self, TOKEN_NUMBER);
}

/**
 * Assuming we already consumed a double quote, try to consume everything up to
 * the closing quote of the same type.
 *
 * This function only supports string literals on the same line.
 * Multi-line string literals are a different beast.
 * Currently we don't support escape sequences and single quotes.
 *
 * III:21.1.2   Expression statements
 *
 * I've now opted to add support for single quote strings, but any given string
 * MUST be surrounded by the same type of quotes.
 */
static Token string_token(Lexer *self, char quote) {
    while (peek_current(self) != quote && !is_at_end(self)) {
        if (peek_current(self) == '\n') {
            return error_token(self, "Unterminated string literal.");
        }
        advance_lexer(self);
    }
    // You can type the nul character with: CTRL+@ (@ = SHIFT+2)
    if (is_at_end(self)) {
        return error_token(self, "Unterminated string literal.");
    }
    // Consume closing quote.
    advance_lexer(self);
    return make_token(self, TOKEN_STRING);
}

#define _match(lex, ch, y, n)       match_lexer(lex, ch) ? (y) : (n)
#define _match2(lex, ch, n, y1, y2) _match(lex, ch, _match(lex, ch, y2, y1), n)
#define _make_token(lex, ch, y, n)  make_token(lex, _match(lex, ch, y, n))
#define make_eq(lex, y, n)          _make_token(lex, '=', y, n)
#define make_dot(lex, n, y1, y2)    make_token(lex, _match2(lex, '.', n, y1, y2))

Token tokenize(Lexer *self) {
    // Don't assign pointers yet, don't want to point at whitespace!
    skip_whitespace(self);
    self->start = self->current;
    if (is_at_end(self)) {
        return make_token(self, TOKEN_EOF);
    }

    char ch = advance_lexer(self);
    if (isidentstarter(ch)) {
        return ident_token(self);
    }
    if (isdigit(ch)) {
        return number_token(self);
    }
    /**
     * III:16.3     A Lexical Grammar for Lox (but adapted for Lua)
     */
    switch (ch) {
    // Balanced pairs
    case '(': return make_token(self, TOKEN_LPAREN);
    case ')': return make_token(self, TOKEN_RPAREN);
    case '{': return make_token(self, TOKEN_LBRACE);
    case '}': return make_token(self, TOKEN_RBRACE);
    case '[': return make_token(self, TOKEN_LBRACKET);
    case ']': return make_token(self, TOKEN_RBRACKET);
    // Punctuation marks
    case ';': return make_token(self, TOKEN_SEMICOL);
    case ':': return make_token(self, TOKEN_COLON);
    case '.': return make_dot(self, TOKEN_PERIOD, TOKEN_CONCAT, TOKEN_VARARGS);
    // Common Arithmetic
    case '+': return make_token(self, TOKEN_PLUS);
    case '-': return make_token(self, TOKEN_DASH);
    case '*': return make_token(self, TOKEN_STAR);
    case '/': return make_token(self, TOKEN_SLASH);
    case '^': return make_token(self, TOKEN_CARET);
    case '%': return make_token(self, TOKEN_PERCENT);

    // Quotation marks
    case '"': return string_token(self, '"');
    case '\'': return string_token(self, '\'');

    // Relational
    case '~': return match_lexer(self, '=')
        ? make_token(self, TOKEN_NEQ) : error_token(self, "Expected '=' after '~'.");
    case '=': return make_eq(self, TOKEN_EQ, TOKEN_ASSIGN);
    case '<': return make_eq(self, TOKEN_LE, TOKEN_LT);
    case '>': return make_eq(self, TOKEN_GE, TOKEN_GT);
    default:  break;
    }
    return error_token(self, "Unexpected character.");
}
