#include <ctype.h>
#include "lexstate.h"

void init_lexstate(LexState *self, jmp_buf *errjmp, const char *name, const char *input) {
    self->token      = (Token){0};
    self->consumed   = (Token){0};
    self->errjmp     = errjmp;
    self->lexeme     = input;
    self->current    = input; // Points to very start of source code string.
    self->name       = name;
    self->linenumber = 1;     // We always assume we start at line 1.
    self->lastline   = 1;
    self->haderror   = false;
}

/* LEXER AND TOKENIZING ------------------------------------------------- {{{ */

#define isidentstarter(ch)  (isalpha(ch) || (ch) == '_')
#define isident(ch)         (isalnum(ch) || (ch) == '_')

static char peek_current(const LexState *self) {
    return *self->current;
}

/**
 * If the current character pointed to in the source code is a nul character, we
 * assumes that we have reached the end of the string. This is particularly nice
 * to help detect syntax errors or unterminated statements in the REPL.
 */
static bool islexEOF(const LexState *self) {
    return *self->current == '\0';
}

static char peek_next(const LexState *self) {
    return islexEOF(self) ? '\0' : *(self->current + 1);
}

/**
 * Increments the `current` then returns the character that was originally being
 * pointed to prior to the increment.
 * 
 * III:24.5.4   Returning from functions
 * 
 * This function now replaces `advance_lexer()` which itself originally replaced
 * `advance()`, which was a static function in `scanner.c` for clox.
 */
static char next_char(LexState *self) {
    return *(self->current++);
}

/**
 * Return `true` if the current character being pointed to matches `expected`.
 * In that case we also increment the `current` character pointer. Otherwise, we
 * return false and modify no other state.
 * 
 * This replaces the `match_token()` function.
 */
static bool match_char(LexState *self, char expected) {
    if (islexEOF(self) || peek_current(self) != expected) {
        return false;
    }
    next_char(self);
    return true;
}

static Token make_token(const LexState *self, TkType type) {
    Token token;
    token.type  = type;
    token.start = self->lexeme;
    token.len   = self->current - self->lexeme;
    return token;
}

static Token error_token(const LexState *self, const char *info) {
    unused(self);
    Token token;
    token.type  = TK_ERROR;
    token.start = info;
    token.len   = strlen(info);
    return token;
}

static void skip_whitespace(LexState *self) {
    for (;;) {
        char ch = peek_current(self);
        switch (ch) {
        case ' ': 
        case '\r': 
        case '\t':
            next_char(self);
            break;
        case '\n':
            self->linenumber++;
            next_char(self);
            break;
        case '-':
            // Singular '-' indicates it's not a Lua-style comment.
            if (peek_next(self) != '-') {
                return;
            }
            // Although comments aren't whitespace we want to ignore them also.
            while (peek_current(self) != '\n' && !islexEOF(self)) {
                next_char(self);
            }
            break;
        default:
            return;
        }
    }
}

typedef struct {
    const char *word;
    size_t len;
} Keyword;

#define makekeyword(literal)   ((Keyword){literal, arraylen(literal) - 1})

static const Keyword keywords[LUA_KEYWORD_COUNT] = {
    [TK_AND]        = makekeyword("and"),
    [TK_BREAK]      = makekeyword("break"),
    [TK_DO]         = makekeyword("do"),
    [TK_ELSE]       = makekeyword("else"),
    [TK_ELSEIF]     = makekeyword("elseif"),
    [TK_END]        = makekeyword("end"),
    [TK_FALSE]      = makekeyword("false"),
    [TK_FOR]        = makekeyword("for"),
    [TK_FUNCTION]   = makekeyword("function"),
    [TK_IF]         = makekeyword("if"),
    [TK_IN]         = makekeyword("in"),
    [TK_LOCAL]      = makekeyword("local"),
    [TK_NIL]        = makekeyword("nil"),
    [TK_NOT]        = makekeyword("not"),
    [TK_OR]         = makekeyword("or"),
    [TK_RETURN]     = makekeyword("return"),
    [TK_SELF]       = makekeyword("self"),
    [TK_THEN]       = makekeyword("then"),
    [TK_TRUE]       = makekeyword("true"),
    [TK_WHILE]      = makekeyword("while"),
};

static TkType check_keyword(const LexState *self, TkType tt, int n) {
    // Avoid indexing out of bounds.
    if (tt >= (TkType)LUA_KEYWORD_COUNT) {
        return TK_IDENT;
    }
    const Keyword *kw = &keywords[tt];
    
    // I am assuming that since current always points somewhere farther down the
    // string, it should be greater than or equal to the lexeme pointer hence it
    // should always be positive.
    if ((size_t)(self->current - self->lexeme) == kw->len) {
        // We use offset to avoid unnecessary comparisons as the switch in the
        // call to `ident_type()` already checked the first character at least.
        if (memcmp(self->lexeme + n, kw->word + n, kw->len - n) == 0) {
            return tt;
        }
    }
    return TK_IDENT;
}

static TkType ident_type(const LexState *self) {
    size_t lexlen = self->current - self->lexeme;
    switch (self->lexeme[0]) {
        case 'a': return check_keyword(self, TK_AND, 1);
        case 'b': return check_keyword(self, TK_BREAK, 1);
        case 'd': return check_keyword(self, TK_DO, 1);
        case 'e': {
            if (lexlen > 1) {
                switch(self->lexeme[1]) {
                case 'l':
                    // This is horrible but I cannot be bothered
                    switch (lexlen) {
                    case 4: return check_keyword(self, TK_ELSE, 2);
                    case 6: return check_keyword(self, TK_ELSEIF, 2);
                    }
                    break;
                case 'n': 
                    return check_keyword(self, TK_END, 2);
                }
            }
        } break;

        case 'f': {
            if (lexlen > 1) {
                switch(self->lexeme[1]) {
                case 'a': return check_keyword(self, TK_FALSE, 2);
                case 'o': return check_keyword(self, TK_FOR, 2);
                case 'u': return check_keyword(self, TK_FUNCTION, 2);
                }
            }
        } break;

        case 'i': {
            if (lexlen > 1) {
                switch (self->lexeme[1]) {
                case 'f': return check_keyword(self, TK_IF, 2);
                case 'n': return check_keyword(self, TK_IN, 2);
                }
            }
        } break;

        case 'l': return check_keyword(self, TK_LOCAL, 1);
        case 'n': {
            if (lexlen > 1) {
                switch (self->lexeme[1]) {
                case 'i': return check_keyword(self, TK_NIL, 2);
                case 'o': return check_keyword(self, TK_NOT, 2);
                }
            }
        } break;

        case 'o': return check_keyword(self, TK_OR, 1);
        case 'r': return check_keyword(self, TK_RETURN, 1);
        case 's': return check_keyword(self, TK_SELF, 1);
        case 't': {
            if (lexlen > 1) {
                switch (self->lexeme[1]) {
                case 'h': return check_keyword(self, TK_THEN, 2);
                case 'r': return check_keyword(self, TK_TRUE, 2);
                }
            }
        } break;

        case 'w': return check_keyword(self, TK_WHILE, 1);
    }
    return TK_IDENT;
}

/**
 * Assuming we consumed an alphabetical/underscore character already, we try to
 * match any number of alphabeticals, numbers or underscores.
 */
static Token ident_token(LexState *self) {
    while (isident(peek_current(self))) {
        next_char(self);
    }
    return make_token(self, ident_type(self));
}

static Token number_token(LexState *self) {
    while (isdigit(peek_current(self))) {
        next_char(self);
    }
    // Look for a fractional part.
    if (peek_current(self) == '.' && isdigit(peek_next(self))) {
        // Consume the '.' character.
        next_char(self);
        while (isdigit(peek_current(self))) {
            next_char(self);
        }
    }
    return make_token(self, TK_NUMBER);
}

static Token string_token(LexState *self, char quote) {
    while (peek_current(self) != quote && !islexEOF(self)) {
        if (peek_current(self) == '\n') {
            return error_token(self, "Unterminated string literal");
        }
        next_char(self);
    }
    // You can type the nul character with: CTRL+@ (@=SHIFT+2)
    if (islexEOF(self)) {
        return error_token(self, "Unterminated string literal");
    }
    // Consume closing quote.
    next_char(self);
    return make_token(self, TK_STRING);
}

/**
 * For use in the other `_match*` and `make_*` macros.
 * 
 * @param lex   A `LexState*`.
 * @param ch    Character literal to match.
 * @param y     The 'yes' or 'true' expression.
 * @param n     The 'no' or 'false' expression.
 */
#define _match(lex, ch, y, n)       match_char(lex, ch) ? (y) : (n)

/**
 * Used to match the same character `ch` twice, e.g. for '.' vs. '..' vs '...'.
 * 
 * e.g. _match2(self, '.', TK_VARARG, TK_CONCAT, TK_PERIOD) expands to:
 *
 * match(self, '.') 
 *      ? match(self, '.') 
 *          ? TK_VARARG 
 *          : TK_CONCAT 
 *      : TK_PERIOD
 */
#define _match2(lex, ch, y2, y1, n) _match(lex, ch, _match(lex, ch, y2, y1), n)
#define make_eq(lex, y, n)          make_token(lex, _match(lex, '=', y, n))
#define make_dot(lex, y2, y1, n)    make_token(lex, _match2(lex, '.', y2, y1, n))

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
static Token tokenize(LexState *self) {
    skip_whitespace(self);
    self->lexeme = self->current;
    if (islexEOF(self)) {
        return make_token(self, TK_EOF);
    }
    char ch = next_char(self);
    if (isidentstarter(ch)) {
        return ident_token(self);
    }
    if (isdigit(ch)) {
        return number_token(self);
    }
    
    switch (ch) {
    // Balanced pairs
    case '(': return make_token(self, TK_LPAREN);
    case ')': return make_token(self, TK_RPAREN);
    case '{': return make_token(self, TK_LCURLY);
    case '}': return make_token(self, TK_RCURLY);
    case '[': return make_token(self, TK_LBRACKET);
    case ']': return make_token(self, TK_RBRACKET);

    // Punctuation marks
    case ';': return make_token(self, TK_SEMICOL);
    case ':': return make_token(self, TK_COLON);
    case '.': return make_dot(self, TK_VARARGS, TK_CONCAT, TK_PERIOD);
    case ',': return make_token(self, TK_COMMA);

    // Common Arithmetic
    case '+': return make_token(self, TK_PLUS);
    case '-': return make_token(self, TK_DASH);
    case '*': return make_token(self, TK_STAR);
    case '/': return make_token(self, TK_SLASH);
    case '^': return make_token(self, TK_CARET);
    case '%': return make_token(self, TK_PERCENT);

    // Quotation marks
    case '"':  return string_token(self, '"');
    case '\'': return string_token(self, '\'');

    // Relational
    case '~': 
        if (match_char(self, '=')) {
            return make_token(self, TK_NEQ);
        }
        return error_token(self, "Expected '=' after '~'");
    case '=': return make_eq(self, TK_EQ, TK_ASSIGN);
    case '<': return make_eq(self, TK_LE, TK_LT);
    case '>': return make_eq(self, TK_GE, TK_GT);
    default:  break;
    }
    return error_token(self, "Unexpected character");
}

/* }}} ---------------------------------------------------------------------- */

/* PARSER AND ERRORS ---------------------------------------------------- {{{ */

void throw_lexerror_at(LexState *self, const Token *token, const char *info) {
    self->haderror = true;
    fprintf(stderr, "%s:%i: %s", self->name, self->lastline, info);
    if (token->type == TK_EOF) {
        fprintf(stderr, ", at end\n");
    } else {
        // We already printed the error token message so try to help the user a 
        // little more by showing them where the error could be near by.
        if (token->type == TK_ERROR) {
            token = &self->consumed;
        }
        // Field precision MUST be of type int.
        fprintf(stderr, ", near '%.*s'\n", (int)token->len, token->start);
    }
    longjmp(*self->errjmp, 1);
}

void throw_lexerror(LexState *self, const char *info) {
    throw_lexerror_at(self, &self->consumed, info);
}

void throw_lexerror_current(LexState *self, const char *info) {
    // Adjust error reporting line to the current one.
    self->lastline = self->linenumber;
    throw_lexerror_at(self, &self->token, info);
}

void next_token(LexState *self) {
    self->lastline = self->linenumber;
    self->consumed = self->token;
    self->token = tokenize(self);
    if (self->token.type == TK_ERROR) {
        // Error tokens already point to an error message literal.
        throw_lexerror_current(self, self->token.start);
    }
}

void consume_token(LexState *self, TkType expected, const char *info) {
    if (check_token(self, expected)) {
        next_token(self);
        return;
    }
    throw_lexerror(self, info);
}

bool match_token_any(LexState *self, const TkType *expected) {
    if (!check_token_any(self, expected)) {
        return false;
    }
    next_token(self);
    return true;
}

bool check_token_any(LexState *self, const TkType *expected) {
    size_t i = 0;
    do {
        if (self->token.type == expected[i]) {
            return true;
        }
        i++;
    } while (expected[i] != TK_EOF); // Sentinel value.
    return false;
}

/* }}} */
