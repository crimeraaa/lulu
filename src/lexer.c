#include <stdarg.h>
#include <ctype.h>
#include "lexer.h"
#include "limits.h"
#include "vm.h"

#define isident(ch)     (isalnum(ch) || (ch) == '_')

typedef const struct {
    const char *word;
    int         len;
} TkInfo;

#define make_tkinfo(s)      {(s), cstr_litsize(s)}

static TkInfo LULU_TKINFO[] = {
    [TK_AND]      = make_tkinfo("and"),
    [TK_BREAK]    = make_tkinfo("break"),
    [TK_DO]       = make_tkinfo("do"),
    [TK_ELSE]     = make_tkinfo("else"),
    [TK_ELSEIF]   = make_tkinfo("elseif"),
    [TK_END]      = make_tkinfo("end"),
    [TK_FALSE]    = make_tkinfo("false"),
    [TK_FOR]      = make_tkinfo("for"),
    [TK_FUNCTION] = make_tkinfo("function"),
    [TK_IF]       = make_tkinfo("if"),
    [TK_IN]       = make_tkinfo("in"),
    [TK_LOCAL]    = make_tkinfo("local"),
    [TK_NIL]      = make_tkinfo("nil"),
    [TK_NOT]      = make_tkinfo("not"),
    [TK_OR]       = make_tkinfo("or"),
    [TK_PRINT]    = make_tkinfo("print"),
    [TK_RETURN]   = make_tkinfo("return"),
    [TK_THEN]     = make_tkinfo("then"),
    [TK_TRUE]     = make_tkinfo("true"),
    [TK_WHILE]    = make_tkinfo("while"),

    [TK_LPAREN]   = make_tkinfo("("),
    [TK_RPAREN]   = make_tkinfo(")"),
    [TK_LBRACKET] = make_tkinfo("["),
    [TK_RBRACKET] = make_tkinfo("]"),
    [TK_LCURLY]   = make_tkinfo("{"),
    [TK_RCURLY]   = make_tkinfo("}"),

    [TK_COMMA]    = make_tkinfo(","),
    [TK_SEMICOL]  = make_tkinfo(";"),
    [TK_VARARG]   = make_tkinfo("..."),
    [TK_CONCAT]   = make_tkinfo(".."),
    [TK_PERIOD]   = make_tkinfo("."),
    [TK_POUND]    = make_tkinfo("#"),

    [TK_PLUS]     = make_tkinfo("+"),
    [TK_DASH]     = make_tkinfo("-"),
    [TK_STAR]     = make_tkinfo("*"),
    [TK_SLASH]    = make_tkinfo("/"),
    [TK_PERCENT]  = make_tkinfo("%"),
    [TK_CARET]    = make_tkinfo("^"),

    [TK_ASSIGN]   = make_tkinfo("="),
    [TK_EQ]       = make_tkinfo("=="),
    [TK_NEQ]      = make_tkinfo("~="),
    [TK_GT]       = make_tkinfo(">"),
    [TK_GE]       = make_tkinfo(">="),
    [TK_LT]       = make_tkinfo("<"),
    [TK_LE]       = make_tkinfo("<="),

    [TK_IDENT]    = make_tkinfo("<identifier>"),
    [TK_STRING]   = make_tkinfo("<string>"),
    [TK_NUMBER]   = make_tkinfo("<number>"),
    [TK_ERROR]    = make_tkinfo("<error>"),
    [TK_EOF]      = make_tkinfo("<eof>"),
};

#undef make_tkinfo

void init_lexer(Lexer *self, const char *input, VM *vm) {
    self->lookahead = compoundlit(Token, 0);
    self->consumed  = self->lookahead;
    self->lexeme    = input;
    self->position  = input;
    self->vm        = vm;
    self->name      = vm->name;
    self->line      = 1;
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
        if (match_char(self, ']')) {
            // `get_nesting()` will mutate `self` so call it first.
            if (get_nesting(self) == nesting && match_char(self, ']')) {
                return;
            }
        }
        // Above call may have fallen through to here as well.
        if (is_at_end(self)) {
            lexerror_at_consumed(self, "Unfinished multiline comment");
            return;
        }
        // Think of this as the iterator increment.
        if (next_char(self) == '\n') {
            self->line += 1;
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
            self->line += 1;
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

static TkType check_keyword(TkType expected, const char *word, int len) {
    TkInfo kw = LULU_TKINFO[expected];
    if (kw.len == len && cstr_equal(kw.word, word, len)) {
        return expected;
    }
    return TK_IDENT;
}

static TkType get_identifier_type(const Lexer *self) {
    const char *word = self->lexeme;
    const int   len  = self->position - self->lexeme;

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
    case '#': return make_token(self, TK_POUND);

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
    self->consumed  = self->lookahead;
    self->lookahead = scan_token(self);
    if (self->lookahead.type == TK_ERROR) {
        lexerror_at_token(self, "Unexpected symbol");
    }
}

typedef struct {
    char  buffer[256];
    char *end;         // +1 past the last written character.
    int   left;        // How many free slots we can still write in.
    int   writes;      // How many slots we have written to so far.
} Builder;

static void init_builder(Builder *self) {
    self->end    = self->buffer;
    self->left   = sizeof(self->buffer);
    self->writes = 0;
}

static void append_builder(Builder *self, const char *format, ...) {
    va_list argp;
    va_start(argp, format);

    self->writes = vsnprintf(self->end, self->left, format, argp);
    self->end   += self->writes;
    self->left  -= self->writes;
    *self->end   = '\0';

    va_end(argp);
}

void expect_token(Lexer *self, TkType expected, const char *info) {
    if (self->lookahead.type == expected) {
        next_token(self);
        return;
    }

    Builder message;
    TkInfo  tkinfo = LULU_TKINFO[expected];

    init_builder(&message);
    append_builder(&message, "Expected '%s'", tkinfo.word);

    if (info != NULL) {
        append_builder(&message, " %s", info);
    }

    lexerror_at_token(self, message.buffer);
}

bool check_token(Lexer *self, TkType expected) {
    TkType actual = self->lookahead.type;
    return actual == expected;
}

bool match_token(Lexer *self, TkType expected) {
    if (check_token(self, expected)) {
        next_token(self);
        return true;
    }
    return false;
}

#undef check_token_any
bool check_token_any(Lexer *self, const TkType expected[]) {
    for (int i = 0; expected[i] != TK_ERROR; i++) {
        if (check_token(self, expected[i])) {
            return true;
        }
    }
    return false;
}

#undef match_token_any
bool match_token_any(Lexer *self, const TkType expected[]) {
    if (check_token_any(self, expected)) {
        next_token(self);
        return true;
    }
    return false;
}


// 1}}} ------------------------------------------------------------------------

// ERROR HANDLING --------------------------------------------------------- {{{1

void lexerror_at(Lexer *self, const Token token, const char *info) {
    fprintf(stderr, "%s:%i: %s", self->name, self->line, info);
    if (token.type == TK_EOF) {
        fprintf(stderr, " at <eof>\n");
    } else {
        fprintf(stderr, " near '%.*s'\n", token.len, token.start);
    }
    longjmp(self->vm->errorjmp, ERROR_COMPTIME);
}

void lexerror_at_token(Lexer *self, const char *info) {
    lexerror_at(self, self->lookahead, info);
}

void lexerror_at_consumed(Lexer *self, const char *info) {
    lexerror_at(self, self->consumed, info);
}

// 1}}} ------------------------------------------------------------------------
