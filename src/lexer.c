#include <stdarg.h>
#include <ctype.h>
#include "lexer.h"
#include "limits.h"
#include "vm.h"

#define isident(ch)     (isalnum(ch) || (ch) == '_')

static const StrView LULU_TKINFO[] = {
    [TK_AND]      = strview_lit("and"),
    [TK_BREAK]    = strview_lit("break"),
    [TK_DO]       = strview_lit("do"),
    [TK_ELSE]     = strview_lit("else"),
    [TK_ELSEIF]   = strview_lit("elseif"),
    [TK_END]      = strview_lit("end"),
    [TK_FALSE]    = strview_lit("false"),
    [TK_FOR]      = strview_lit("for"),
    [TK_FUNCTION] = strview_lit("function"),
    [TK_IF]       = strview_lit("if"),
    [TK_IN]       = strview_lit("in"),
    [TK_LOCAL]    = strview_lit("local"),
    [TK_NIL]      = strview_lit("nil"),
    [TK_NOT]      = strview_lit("not"),
    [TK_OR]       = strview_lit("or"),
    [TK_PRINT]    = strview_lit("print"),
    [TK_RETURN]   = strview_lit("return"),
    [TK_THEN]     = strview_lit("then"),
    [TK_TRUE]     = strview_lit("true"),
    [TK_WHILE]    = strview_lit("while"),

    [TK_LPAREN]   = strview_lit("("),
    [TK_RPAREN]   = strview_lit(")"),
    [TK_LBRACKET] = strview_lit("["),
    [TK_RBRACKET] = strview_lit("]"),
    [TK_LCURLY]   = strview_lit("{"),
    [TK_RCURLY]   = strview_lit("}"),

    [TK_COMMA]    = strview_lit(","),
    [TK_SEMICOL]  = strview_lit(";"),
    [TK_VARARG]   = strview_lit("..."),
    [TK_CONCAT]   = strview_lit(".."),
    [TK_PERIOD]   = strview_lit("."),
    [TK_POUND]    = strview_lit("#"),

    [TK_PLUS]     = strview_lit("+"),
    [TK_DASH]     = strview_lit("-"),
    [TK_STAR]     = strview_lit("*"),
    [TK_SLASH]    = strview_lit("/"),
    [TK_PERCENT]  = strview_lit("%"),
    [TK_CARET]    = strview_lit("^"),

    [TK_ASSIGN]   = strview_lit("="),
    [TK_EQ]       = strview_lit("=="),
    [TK_NEQ]      = strview_lit("~="),
    [TK_GT]       = strview_lit(">"),
    [TK_GE]       = strview_lit(">="),
    [TK_LT]       = strview_lit("<"),
    [TK_LE]       = strview_lit("<="),

    [TK_IDENT]    = strview_lit("<identifier>"),
    [TK_STRING]   = strview_lit("<string>"),
    [TK_NUMBER]   = strview_lit("<number>"),
    [TK_ERROR]    = strview_lit("<error>"),
    [TK_EOF]      = strview_lit("<eof>"),
};

static void init_strview(StrView *self, const char *view)
{
    self->begin = view;
    self->end   = view;
    self->len   = 0;
}

static void init_token(Token *self)
{
    init_strview(&self->view, NULL);
    self->line  = 0;
    self->type  = TK_EOF;
}

void init_lexer(Lexer *self, const char *input, VM *vm)
{
    init_token(&self->lookahead);
    init_token(&self->consumed);
    init_strview(&self->lexeme, input);
    self->vm     = vm;
    self->name   = vm->name;
    self->string = NULL;
    self->number = 0;
    self->line   = 1;
}

// HELPERS ---------------------------------------------------------------- {{{1

static bool is_at_end(const Lexer *self)
{
    return *self->lexeme.end == '\0';
}

static char peek_current_char(const Lexer *self)
{
    return *self->lexeme.end;
}

static char peek_next_char(const Lexer *self)
{
    return *(self->lexeme.end + 1);
}

// Analogous to `scanner.c:advance()` in the book.
static char next_char(Lexer *self)
{
    self->lexeme.len += 1;
    self->lexeme.end += 1;
    return *(self->lexeme.end - 1);
}

static bool match_char(Lexer *self, char expected)
{
    if (is_at_end(self) || peek_current_char(self) != expected) {
        return false;
    } else {
        next_char(self);
        return true;
    }
}

static void singleline(Lexer *self)
{
    while (peek_current_char(self) != '\n' && !is_at_end(self)) {
        next_char(self);
    }
}

// Assuming we've consumed a `"[["`, check its bracket nesting level.
// Note this will also mutate state, so be wary of the order you call it in.
static int get_nesting(Lexer *self)
{
    int nesting = 0;
    while (match_char(self, '=')) {
        nesting++;
    }
    return nesting;
}

// Consume a multi-line string or comment with a known nesting level.
static void multiline(Lexer *self, int nesting)
{
    for (;;) {
        if (match_char(self, ']')) {
            // `get_nesting()` will mutate `self` so call it first.
            if (get_nesting(self) == nesting && match_char(self, ']')) {
                return;
            }
        }
        // Above call may have fallen through to here as well.
        if (is_at_end(self)) {
            lexerror_at_middle(self, "Unfinished multiline sequence");
            return;
        }
        // Think of this as the iterator increment.
        if (next_char(self) == '\n') {
            self->line += 1;
        }
    }
}

static void skip_comment(Lexer *self)
{
    // Look for the first '[' which starts off a multiline string/comment.
    if (match_char(self, '[')) {
        int nesting = get_nesting(self);
        if (match_char(self, '[')) {
            multiline(self, nesting);
            return;
        }
    }
    // Didn't match 2 '['. Fall through case.
    singleline(self);
}

// 1}}} ------------------------------------------------------------------------

// TOKENIZER -------------------------------------------------------------- {{{1

static Token make_token(Lexer *self, TkType type)
{
    Token token;
    token.view = self->lexeme;
    token.type = type;
    token.line = self->line;
    return token;
}

static Token error_token(Lexer *self)
{
    Token token = make_token(self, TK_ERROR);
    // For error tokens, report only the first line if this is a multiline.
    char *newline = strchr(token.view.begin, '\n');
    if (newline != NULL) {
        token.view.end = newline;
        token.view.len = cast(int, token.view.end - token.view.begin);
    }
    return token;
}

static void skip_whitespace(Lexer *self)
{
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

static TkType check_keyword(TkType expected, StrView word)
{
    StrView keyword = LULU_TKINFO[expected];
    if (keyword.len == word.len && cstr_eq(keyword.begin, word.begin, word.len)) {
        return expected;
    }
    return TK_IDENT;
}

static TkType get_identifier_type(const Lexer *self)
{
    StrView word = self->lexeme;
    switch (word.begin[0]) {
    case 'a': return check_keyword(TK_AND, word);
    case 'b': return check_keyword(TK_BREAK, word);
    case 'd': return check_keyword(TK_DO, word);
    case 'e':
        switch (word.len) {
        case cstr_len("end"):
            return check_keyword(TK_END, word);
        case cstr_len("else"):
            return check_keyword(TK_ELSE, word);
        case cstr_len("elseif"):
            return check_keyword(TK_ELSEIF, word);
        }
        break;
    case 'f':
        if (word.len > 1) {
            switch (word.begin[1]) {
            case 'a': return check_keyword(TK_FALSE, word);
            case 'o': return check_keyword(TK_FOR, word);
            case 'u': return check_keyword(TK_FUNCTION, word);
            }
        }
        break;
    case 'i':
        if (word.len > 1) {
            switch (word.begin[1]) {
            case 'f': return check_keyword(TK_IF, word);
            case 'n': return check_keyword(TK_IN, word);
            }
        }
        break;
    case 'l': return check_keyword(TK_LOCAL, word);
    case 'n':
        if (word.len > 1) {
            switch (word.begin[1]) {
            case 'i': return check_keyword(TK_NIL, word);
            case 'o': return check_keyword(TK_NOT, word);
            }
        }
        break;
    case 'o': return check_keyword(TK_OR, word);
    case 'p': return check_keyword(TK_PRINT, word);
    case 'r': return check_keyword(TK_RETURN, word);
    case 't':
        if (word.len > 1) {
            switch (word.begin[1]) {
            case 'h': return check_keyword(TK_THEN, word);
            case 'r': return check_keyword(TK_TRUE, word);
            }
        }
        break;
    case 'w': return check_keyword(TK_WHILE, word);
    }
    return TK_IDENT;
}

static Token identifier_token(Lexer *self)
{
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
 */
static void decimal_sequence(Lexer *self)
{
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
        // Must have at least 1 digit but that's a problem for later.
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
static Token number_token(Lexer *self)
{
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
    char *end;
    self->number = cstr_tonumber(self->lexeme.begin, &end);
    // If this is true, strtod failed to convert the entire token/lexeme.
    if (end != self->lexeme.end) {
        lexerror_at_middle(self, "Malformed number");
    }
    return make_token(self, TK_NUMBER);
}

static Token string_token(Lexer *self, char quote)
{
    while (peek_current_char(self) != quote && !is_at_end(self)) {
        if (peek_current_char(self) == '\n') {
            goto bad_string;
        }
        next_char(self);
    }

    // The label isn't actually in its own block scope.
    if (is_at_end(self)) {
bad_string:
        lexerror_at_middle(self, "Unterminated string");
    }

    // Consume closing quote.
    next_char(self);

    // Left +1 to skip left quote, len -2 to get offset of last non-quote.
    StrView view = make_strview(self->lexeme.begin + 1, self->lexeme.len - 2);
    self->string = copy_string(self->vm, &view);
    return make_token(self, TK_STRING);
}

static Token lstring_token(Lexer *self, int nesting)
{
    bool open = match_char(self, '\n');
    if (open) {
        self->line++;
    }
    multiline(self, nesting);

    // Skip "[[" and '=' nesting, as well as "]]" and '=' - 2 for last index.
    int     mark = nesting + open + 2;
    int     len  = self->lexeme.len - mark - 2;
    StrView view = make_strview(self->lexeme.begin + mark, len);
    self->string = copy_lstring(self->vm, &view);
    return make_token(self, TK_STRING);
}

#define make_ifeq(lexer, ch, y, n) \
    make_token(lexer, match_char(lexer, ch) ? (y) : (n))

Token scan_token(Lexer *self)
{
    skip_whitespace(self);
    init_strview(&self->lexeme, self->lexeme.end);
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
    case '[': {
        int nesting = get_nesting(self);
        if (match_char(self, '[')) {
            return lstring_token(self, nesting);
        }
        return make_token(self, TK_LBRACKET);
    }
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
            lexerror_at_middle(self, "Expected '='");
        }
    case '>': return make_ifeq(self, '=', TK_GE, TK_GT);
    case '<': return make_ifeq(self, '=', TK_LE, TK_LT);

    case '\"': return string_token(self, '\"');
    case '\'': return string_token(self, '\'');
    default:   return error_token(self);
    }
}

void next_token(Lexer *self)
{
    self->consumed  = self->lookahead;
    self->lookahead = scan_token(self);
    if (self->lookahead.type == TK_ERROR) {
        lexerror_at_lookahead(self, "Unexpected symbol");
    }
}

typedef struct {
    char  buffer[256];
    char *end;         // +1 past the last written character.
    int   left;        // How many free slots we can still write in.
    int   writes;      // How many slots we have written to so far.
} Builder;

static void init_builder(Builder *self)
{
    self->end    = self->buffer;
    self->left   = sizeof(self->buffer);
    self->writes = 0;
}

static void append_builder(Builder *self, const char *format, ...)
{
    va_list argp;
    va_start(argp, format);

    self->writes = vsnprintf(self->end, self->left, format, argp);
    self->end   += self->writes;
    self->left  -= self->writes;
    *self->end   = '\0';

    va_end(argp);
}

void expect_token(Lexer *self, TkType expected, const char *info)
{
    if (self->lookahead.type == expected) {
        next_token(self);
        return;
    }

    Builder message;
    StrView tkinfo = LULU_TKINFO[expected];

    init_builder(&message);
    append_builder(&message, "Expected '%s'", tkinfo.begin);

    if (info != NULL) {
        append_builder(&message, " %s", info);
    }

    lexerror_at_lookahead(self, message.buffer);
}

bool check_token(Lexer *self, TkType expected)
{
    TkType actual = self->lookahead.type;
    return actual == expected;
}

bool match_token(Lexer *self, TkType expected)
{
    if (check_token(self, expected)) {
        next_token(self);
        return true;
    }
    return false;
}

#undef check_token_any
bool check_token_any(Lexer *self, const TkType expected[])
{
    for (int i = 0; expected[i] != TK_ERROR; i++) {
        if (check_token(self, expected[i])) {
            return true;
        }
    }
    return false;
}

#undef match_token_any
bool match_token_any(Lexer *self, const TkType expected[])
{
    if (check_token_any(self, expected)) {
        next_token(self);
        return true;
    }
    return false;
}


// 1}}} ------------------------------------------------------------------------

// ERROR HANDLING --------------------------------------------------------- {{{1

void lexerror_at(Lexer *self, const Token *token, const char *info)
{
    eprintf("%s:%i: %s", self->name, self->line, info);
    if (token->type == TK_EOF) {
        eprintln(" at <eof>");
    } else {
        eprintfln(" near '%.*s'", token->view.len, token->view.begin);
    }
    longjmp(self->vm->errorjmp, ERROR_COMPTIME);
}

void lexerror_at_lookahead(Lexer *self, const char *info)
{
    lexerror_at(self, &self->lookahead, info);
}

void lexerror_at_consumed(Lexer *self, const char *info)
{
    lexerror_at(self, &self->consumed, info);
}

void lexerror_at_middle(Lexer *self, const char *info)
{
    Token token = error_token(self);
    lexerror_at(self, &token, info);
}


// 1}}} ------------------------------------------------------------------------
