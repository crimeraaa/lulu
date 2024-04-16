#include <stdarg.h>
#include <ctype.h>
#include "lexer.h"
#include "compiler.h"
#include "limits.h"
#include "vm.h"

#define isident(ch)     (isalnum(ch) || (ch) == '_')

void init_lexer(Lexer *self, const char *input, struct VM *vm) {
    self->token    = compoundlit(Token, 0);
    self->consumed = self->token;
    self->lexeme   = input;
    self->position = input;
    self->vm       = vm;
    self->name     = vm->name;
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

// Return current character then increment the Lexer's position pointer.
// This is the same as `advance()` in the book.
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
        if (is_at_end(self)) {
            lexerror_at_consumed(self, "Unfinished multiline comment");
            return;
        }

        // If all went well we can safely consume this character.
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

// static Token error_token(const Lexer *self, const char *info) {
//     Token token;
//     token.start = info;
//     token.len   = cast(int, strlen(info));
//     token.line  = self->line;
//     token.type  = TK_ERROR;
//     return token;
// }

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

typedef struct {
    const char *word;
    int len;
} Keyword;

#define make_keyword(s)     (Keyword){s, cstr_litsize(s)}

static const Keyword KEYWORDS[] = {
    [TK_AND]      = make_keyword("and"),
    [TK_BREAK]    = make_keyword("break"),
    [TK_DO]       = make_keyword("do"),
    [TK_ELSE]     = make_keyword("else"),
    [TK_ELSEIF]   = make_keyword("elseif"),
    [TK_END]      = make_keyword("end"),
    [TK_FALSE]    = make_keyword("false"),
    [TK_FOR]      = make_keyword("for"),
    [TK_FUNCTION] = make_keyword("function"),
    [TK_IF]       = make_keyword("if"),
    [TK_IN]       = make_keyword("in"),
    [TK_LOCAL]    = make_keyword("local"),
    [TK_NIL]      = make_keyword("nil"),
    [TK_NOT]      = make_keyword("not"),
    [TK_OR]       = make_keyword("or"),
    [TK_PRINT]    = make_keyword("print"),
    [TK_RETURN]   = make_keyword("return"),
    [TK_THEN]     = make_keyword("then"),
    [TK_TRUE]     = make_keyword("true"),
    [TK_WHILE]    = make_keyword("while"),
};

static_assert(array_len(KEYWORDS) == NUM_KEYWORDS, "Bad keyword count");

static TkType check_keyword(TkType expect, const char *word, int len) {
    const Keyword *kw = &KEYWORDS[expect];
    if (kw->len == len && cstr_equal(kw->word, word, len)) {
        return expect;
    }
    return TK_IDENT;
}

static TkType get_identifier_type(const Lexer *self) {
    const char *word = self->lexeme;
    const int len = self->position - self->lexeme;

    switch (word[0]) {
    case 'a': return check_keyword(TK_AND, word, len);
    case 'b': return check_keyword(TK_BREAK, word, len);
    case 'd': return check_keyword(TK_DO, word, len);
    case 'e':
        switch (len) {
        case cstr_litsize("end"):
            return check_keyword(TK_END, word, len);
        case cstr_litsize("else"):
            return check_keyword(TK_ELSE, word, len);
        case cstr_litsize("elseif"):
            return check_keyword(TK_ELSEIF, word, len);
        }
        break;
    case 'f':
        if (len > 1) {
            switch (word[1]) {
            case 'a': return check_keyword(TK_FALSE, word, len);
            case 'o': return check_keyword(TK_FOR, word, len);
            case 'u': return check_keyword(TK_FUNCTION, word, len);
            }
        }
        break;
    case 'i':
        if (len > 1) {
            switch (word[1]) {
            case 'f': return check_keyword(TK_IF, word, len);
            case 'n': return check_keyword(TK_IN, word, len);
            }
        }
        break;
    case 'l': return check_keyword(TK_LOCAL, word, len);
    case 'n':
        if (len > 1) {
            switch (word[1]) {
            case 'i': return check_keyword(TK_NIL, word, len);
            case 'o': return check_keyword(TK_NOT, word, len);
            }
        }
        break;
    case 'o': return check_keyword(TK_OR, word, len);
    case 'p': return check_keyword(TK_PRINT, word, len);
    case 'r': return check_keyword(TK_RETURN, word, len);
    case 't':
        if (len > 1) {
            switch (word[1]) {
            case 'h': return check_keyword(TK_THEN, word, len);
            case 'r': return check_keyword(TK_TRUE, word, len);
            }
        }
        break;
    case 'w': return check_keyword(TK_WHILE, word, len);
    }
    return TK_IDENT;
}

static Token identifier_token(Lexer *self) {
    while (isident(peek_current_char(self))) {
        next_char(self);
    }
    return make_token(self, get_identifier_type(self));
}

/**
 * @brief   Consume a sequence of characters in the regular expression:
 *          ` [0-9]+(e(+|-)?[0-9]+)? `.
 *
 * @details Although recursive, it's designed to accept the following forms:
 *
 *          1. Integer literals     := 1, 13, 45000, 65536, 127
 *          2. Integer w/ Exponent  := 1e2, 3e+4, 5e-6, 13e900
 *          3. Float literals       := 1.2, 3.14, .5, 0.00013, 6.32, 9.81, .45
 *          4. Float with Exponent  := .2e3, 1.2e+3, 3.14e-2, 6.022e23
 *
 *          We also consume the following invalid forms for later errors:
 *
 *          1. Empty exponent       := 1e
 *          2. Invalid digits       := 13fgi, 4e12ijslkd
 *          3. Multiple periods     := 1.2.3.4
 *          4. Python/JS separators := 1_000_000, 4_294_967_295
 *
 * @note    This function does not report errors on its own, that will be the
 *          responsibility of the compiler.
 */
static void decimal_sequence(Lexer *self) {
    while (isdigit(peek_current_char(self))) {
        next_char(self);
    }

    // Have an exponent? It CANNOT come after the decimal point.
    if (match_char(self, 'e')) {
        char ch = peek_current_char(self);
        // Explicit signedness is optional.
        if (ch == '+' || ch == '-') {
            next_char(self);
        }
        // Must have at least 1 digit but that's a problem for the compiler.
        decimal_sequence(self);
        return;
    }

    // Have a fraction? This sequence of digits can also have an exponent.
    if (match_char(self, '.')) {
        decimal_sequence(self);
        return;
    }
}

/**
 * @brief   Assumes we already consumed a digit character and are pointing at
 *          the first character right after it.
 */
static Token number_token(Lexer *self) {
    // Does not verify if we had a `0` character before, but whatever
    if (match_char(self, 'x')) {
        while (isxdigit(peek_current_char(self))) {
            next_char(self);
        }
    } else {
        decimal_sequence(self);
    }
    // Consume any trailing characters for error handling later on.
    while (isident(peek_current_char(self))) {
        next_char(self);
    }
    return make_token(self, TK_NUMBER);
}

static Token string_token(Lexer *self, char quote) {
    while (peek_current_char(self) != quote && !is_at_end(self)) {
        if (peek_current_char(self) == '\n') {
            lexerror_at_consumed(self, "Unfinished string");
        }
        next_char(self);
    }

    if (is_at_end(self)) {
        lexerror_at_consumed(self, "Unfinished string");
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
    if (isalpha(ch) || ch == '_') {
        return identifier_token(self);
    }

    switch (ch) {
    case '(': return make_token(self, TK_LPAREN);
    case ')': return make_token(self, TK_RPAREN);
    case '[': return make_token(self, TK_LBRACKET);
    case ']': return make_token(self, TK_RBRACKET);
    case '{': return make_token(self, TK_LCURLY);
    case '}': return make_token(self, TK_RCURLY);

    case ',': return make_token(self, TK_COMMA);
    case ';': return make_token(self, TK_SEMICOL);
    case '.':
        if (match_char(self, '.')) {
            return make_ifeq(self, '.', TK_VARARG, TK_CONCAT);
        } else if (isdigit(peek_current_char(self))) {
            // Have a decimal literal with no leading digits, e.g. `.1`.
            return number_token(self);
        } else {
            return make_token(self, TK_PERIOD);
        }

    case '+': return make_token(self, TK_PLUS);
    case '-': return make_token(self, TK_DASH);
    case '*': return make_token(self, TK_STAR);
    case '/': return make_token(self, TK_SLASH);
    case '%': return make_token(self, TK_PERCENT);
    case '^': return make_token(self, TK_CARET);

    case '=': return make_ifeq(self, '=', TK_EQ, TK_ASSIGN);
    case '~':
        if (match_char(self, '=')) {
            return make_token(self, TK_NEQ);
        } else {
            lexerror_at_consumed(self, "Expected '=' after '~'");
        }
    case '>': return make_ifeq(self, '=', TK_GE, TK_GT);
    case '<': return make_ifeq(self, '=', TK_LE, TK_LT);

    case '\"': return string_token(self, '\"');
    case '\'': return string_token(self, '\'');
    default:   return make_token(self, TK_ERROR);
    }
}

void next_token(Lexer *self) {
    self->consumed = self->token;
    self->token    = scan_token(self);
    if (self->token.type == TK_ERROR) {
        lexerror_at_token(self, "Unexpected symbol");
    }
}

void consume_token(Lexer *self, TkType expected, const char *info) {
    if (self->token.type == expected) {
        next_token(self);
    } else {
        lexerror_at_token(self, info);
    }
}

#undef check_token
bool check_token(Lexer *self, const TkType expected[]) {
    TkType actual = self->token.type;
    int i = 0;
    do {
        if (actual == expected[i]) {
            return true;
        }
        i++;
    } while (expected[i] != TK_EOF); //Sentinel value.
    return false;
}

#undef match_token
bool match_token(Lexer *self, const TkType expected[]) {
    if (check_token(self, expected)) {
        next_token(self);
        return true;
    }
    return false;
}


// 1}}} ------------------------------------------------------------------------

// ERROR HANDLING --------------------------------------------------------- {{{1

void lexerror_at(Lexer *self, const Token *token, const char *info) {
    fprintf(stderr, "%s:%i: %s", self->name, self->line, info);
    if (token->type == TK_EOF) {
        fprintf(stderr, " at end\n");
    } else {
        fprintf(stderr, " near '%.*s'\n", token->len, token->start);
    }
    longjmp(self->vm->errorjmp, ERROR_COMPTIME);
}

void lexerror_at_token(Lexer *self, const char *info) {
    lexerror_at(self, &self->token, info);
}

void lexerror_at_consumed(Lexer *self, const char *info) {
    lexerror_at(self, &self->consumed, info);
}

// 1}}} ------------------------------------------------------------------------
