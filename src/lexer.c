#include <ctype.h>
#include "lexer.h"
#include "limits.h"

#define isident(ch)     (isalnum(ch) || (ch) == '_')

void init_lexer(Lexer *self, const char *input, const char *name) {
    self->lexeme   = input;
    self->position = input; 
    self->name     = name;
    self->line     = 1;
}

// HELPERS ---------------------------------------------------------------- {{{1

static bool is_at_end(const Lexer *self) {
    return *self->position == '\0';
}

static char peek_current_char(const Lexer *self) {
    return *self->position;
}

static char peek_next_char(const Lexer *self) {
    return *(self->position + 1);
}

// The same as `advance()` in the book.
static char next_char(Lexer *self) {
    return *self->position++;
}

static bool match_char(Lexer *self, char expected) {
    if (is_at_end(self) || *self->position != expected) {
        return false;
    } else {
        next_char(self);
        return true;
    }
}

// 1}}} ------------------------------------------------------------------------

// TOKENIZER -------------------------------------------------------------- {{{1

static Token make_token(const Lexer *self, TkType type) {
    Token token;
    token.start = self->lexeme;
    token.len   = cast(int, self->position - self->lexeme);    
    token.line  = self->line;
    token.type  = type;
    return token;
}

static Token error_token(const Lexer *self, const char *info) {
    Token token;
    token.start = info;
    token.len   = cast(int, strlen(info));
    token.line  = self->line;
    token.type  = TK_ERROR;
    return token;
}

static void singleline(Lexer *self) {
    while (peek_current_char(self) != '\n' && !is_at_end(self)) {
        next_char(self);
    }
}

// Assuming we've consumed a `"[["`, check its bracket nesting level.
// Note this will also mutate state, so be wary of the order you call it in.
static int get_nesting(Lexer *self) {
    int nesting = 0;
    while (match_char(self, '=')) {
        nesting++;
    }
    return nesting;
}

// Consume a multi-line string or comment with a known nesting level.
static void multiline(Lexer *self, int nesting) {
    for (;;) {
        char ch = peek_current_char(self);
        if (ch == ']') {
            next_char(self);
            int closing = get_nesting(self);
            if (match_char(self, ']') && closing == nesting) {
                return;
            }
        }
        // Above call may have fallen through to here as well.
        // TODO: Need to throw error here, using `longjmp`
        if (is_at_end(self)) {
            return;
        }

        next_char(self);
        if (ch == '\n') {
            self->line++;
        }
    }
}

static void skip_comment(Lexer *self) {
    // Look for the first '[' which starts off a multiline string/comment.
    if (match_char(self, '[')) {
        int nesting = get_nesting(self);
        if (match_char(self, '[')) {
            multiline(self, nesting);     
        } else {
            singleline(self);
        }
    } else {
        singleline(self);
    }
}

static void skip_whitespace(Lexer *self) {
    for (;;) {
        char ch = peek_current_char(self);
        switch (ch) {
        case ' ':
        case '\r':
        case '\t':
            next_char(self);
            break;
        case '\n':
            self->line++;
            next_char(self);
            break;
        case '-':
            if (peek_next_char(self) != '-') {
                return;
            }
            // Skip the 2 '-' characters so we are at the comment's contents.
            next_char(self);
            next_char(self);
            skip_comment(self);
            break;
        default:
            return;
        }
    }
}

static TkType get_identifier_type(Lexer *self) {
    unused(self);
    return TK_IDENT;
}

static Token identifier_token(Lexer *self) {
    while (isident(peek_current_char(self))) {
        next_char(self);
    }
    return make_token(self, get_identifier_type(self));
}

static Token number_token(Lexer *self) {
    while (isdigit(peek_current_char(self))) {
        next_char(self);     
    }
    
    // Look for a fractional part, empty fractions like `1.` are allowed.
    if (match_char(self, '.')) {
        // Consume the '.' character.
        next_char(self);
        while (isdigit(peek_current_char(self))) {
            next_char(self);
        }
    }
    return make_token(self, TK_NUMBER);
}

static Token string_token(Lexer *self, char quote) {
    char ch;
    while ((ch = peek_current_char(self)) != quote && !is_at_end(self)) {
        if (ch == '\n') {
            return error_token(self, "Unterminated string literal");
        }
        next_char(self);
    }
    
    if (is_at_end(self)) {
        return error_token(self, "Unterminated string literal");
    }
    
    // Consume closing quote.
    next_char(self);
    return make_token(self, TK_STRING);
}

#define make_ifeq(lexer, ch, y, n) \
    make_token(lexer, match_char(lexer, ch) ? (y) : (n))

Token scan_token(Lexer *self) {
    skip_whitespace(self);
    self->lexeme = self->position;
    if (is_at_end(self)) {
        return make_token(self, TK_EOF);
    }
    
    char ch = next_char(self);
    if (isdigit(ch)) {
        return number_token(self);
    }
    // Identifiers and keywords can only start with alpha and underscore.
    // However past that, digits are also allowed.
    if (isalpha(ch) || ch == '_') {
        return identifier_token(self);
    }
    switch (ch) {
    case '(':   return make_token(self, TK_LPAREN);
    case ')':   return make_token(self, TK_RPAREN);
    case '[':   return make_token(self, TK_LBRACKET);
    case ']':   return make_token(self, TK_RBRACKET);
    case '{':   return make_token(self, TK_LCURLY);
    case '}':   return make_token(self, TK_RCURLY);

    case ',':   return make_token(self, TK_COMMA);
    case ';':   return make_token(self, TK_SEMICOL);
    case '.':
        if (match_char(self, '.')) {
            return make_ifeq(self, '.', TK_VARARG, TK_CONCAT);
        } else {
            return make_token(self, TK_PERIOD);
        }
                
    case '+':   return make_token(self, TK_PLUS);
    case '-':   return make_token(self, TK_DASH);
    case '*':   return make_token(self, TK_STAR);
    case '/':   return make_token(self, TK_SLASH);
    case '%':   return make_token(self, TK_PERCENT);
    case '^':   return make_token(self, TK_CARET);
                
    case '=':   return make_ifeq(self, '=', TK_EQ, TK_ASSIGN);
    case '~':
        if (match_char(self, '=')) {
            return make_token(self, TK_NEQ);
        } else {
            return error_token(self, "Expected '=' after '~'");
        }
    case '>':   return make_ifeq(self, '=', TK_GE, TK_GT);
    case '<':   return make_ifeq(self, '=', TK_LE, TK_LT);
                
    case '"':   return string_token(self, '"');
    case '\'':  return string_token(self, '\'');
    }
    return error_token(self, "Unexpected symbol");
}

// 1}}} ------------------------------------------------------------------------
