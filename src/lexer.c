#include <stdarg.h>
#include <ctype.h>
#include "lexer.h"
#include "limits.h"
#include "string.h"
#include "table.h"
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

static void init_strview(StrView *sv, const char *src)
{
    sv->begin = src;
    sv->end   = src;
    sv->len   = 0;
}

static void init_token(Token *tk)
{
    init_strview(&tk->view, NULL);
    tk->line  = 0;
    tk->type  = TK_EOF;
}

void init_lexer(Lexer *ls, const char *input, struct lulu_VM *vm)
{
    init_token(&ls->lookahead);
    init_token(&ls->consumed);
    init_strview(&ls->lexeme, input);
    ls->vm     = vm;
    ls->name   = vm->name;
    ls->string = NULL;
    ls->number = 0;
    ls->line   = 1;
}

// HELPERS ---------------------------------------------------------------- {{{1

static bool is_at_end(const Lexer *ls)
{
    return *ls->lexeme.end == '\0';
}

static char peek_current_char(const Lexer *ls)
{
    return *ls->lexeme.end;
}

static char peek_next_char(const Lexer *ls)
{
    return *(ls->lexeme.end + 1);
}

// Analogous to `scanner.c:advance()` in the book.
static char next_char(Lexer *ls)
{
    ls->lexeme.len += 1;
    ls->lexeme.end += 1;
    return *(ls->lexeme.end - 1);
}

static bool match_char(Lexer *ls, char ch)
{
    if (is_at_end(ls) || peek_current_char(ls) != ch) {
        return false;
    } else {
        next_char(ls);
        return true;
    }
}

static void singleline(Lexer *ls)
{
    while (peek_current_char(ls) != '\n' && !is_at_end(ls)) {
        next_char(ls);
    }
}

// Assuming we've consumed a `"[["`, check its bracket nesting level.
// Note this will also mutate state, so be wary of the order you call it in.
static int get_nesting(Lexer *ls)
{
    int nesting = 0;
    while (match_char(ls, '=')) {
        nesting++;
    }
    return nesting;
}

// Consume a multi-line string or comment with a known nesting level.
static void multiline(Lexer *ls, int lvl)
{
    for (;;) {
        if (match_char(ls, ']')) {
            int close = get_nesting(ls);
            if (close == lvl && match_char(ls, ']')) {
                return;
            }
        }
        // Above call may have fallen through to here as well.
        if (is_at_end(ls)) {
            lexerror_at_middle(ls, "Unfinished multiline sequence");
            return;
        }
        // Think of this as the iterator increment.
        if (next_char(ls) == '\n') {
            ls->line += 1;
        }
    }
}

static void skip_comment(Lexer *ls)
{
    // Look for the first '[' which starts off a multiline string/comment.
    if (match_char(ls, '[')) {
        int lvl = get_nesting(ls);
        if (match_char(ls, '[')) {
            multiline(ls, lvl);
            return;
        }
    }
    // Didn't match 2 '['. Fall through case.
    singleline(ls);
}

// 1}}} ------------------------------------------------------------------------

// TOKENIZER -------------------------------------------------------------- {{{1

static Token make_token(Lexer *ls, TkType type)
{
    Token tk;
    tk.view = ls->lexeme;
    tk.type = type;
    tk.line = ls->line;
    return tk;
}

static Token error_token(Lexer *ls)
{
    Token tk = make_token(ls, TK_ERROR);

    // For error tokens, report only the first line if this is a multiline.
    const char *newline = memchr(tk.view.begin, '\n', tk.view.len);
    if (newline != NULL) {
        tk.view.end = newline;
        tk.view.len = cast(int, tk.view.end - tk.view.begin);
    }
    return tk;
}

static void skip_whitespace(Lexer *ls)
{
    for (;;) {
        char ch = peek_current_char(ls);
        switch (ch) {
        case '\n': ls->line += 1; // fall through
        case ' ':
        case '\r':
        case '\t': next_char(ls); break;
        case '-':
            if (peek_next_char(ls) != '-') {
                return;
            }
            // Skip the 2 '-' characters so we are at the comment's contents.
            next_char(ls);
            next_char(ls);
            skip_comment(ls);
            break;
        default:
            return;
        }
    }
}

static TkType check_keyword(TkType type, StrView word)
{
    StrView kw = LULU_TKINFO[type];
    if (kw.len == word.len && cstr_eq(kw.begin, word.begin, word.len)) {
        return type;
    }
    return TK_IDENT;
}

static TkType get_identifier_type(const Lexer *ls)
{
    StrView word = ls->lexeme;
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

static Token identifier_token(Lexer *ls)
{
    while (isident(peek_current_char(ls))) {
        next_char(ls);
    }
    return make_token(ls, get_identifier_type(ls));
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
static void decimal_sequence(Lexer *ls)
{
    while (isdigit(peek_current_char(ls))) {
        next_char(ls);
    }

    // Have an exponent? It CANNOT come after the decimal point.
    if (match_char(ls, 'e')) {
        char ch = peek_current_char(ls);
        // Explicit signedness is optional.
        if (ch == '+' || ch == '-') {
            next_char(ls);
        }
        // Must have at least 1 digit but that's a problem for later.
        decimal_sequence(ls);
        return;
    }

    // Have a fraction? This sequence of digits can also have an exponent.
    if (match_char(ls, '.')) {
        decimal_sequence(ls);
        return;
    }
}

/**
 * @brief   Assumes we already consumed a digit character and are pointing at
 *          the first character right after it.
 */
static Token number_token(Lexer *ls)
{
    // Does not verify if we had a `0` character before, but whatever
    if (match_char(ls, 'x')) {
        while (isxdigit(peek_current_char(ls))) {
            next_char(ls);
        }
    } else {
        decimal_sequence(ls);
    }
    // Consume any trailing characters for error handling later on.
    while (isident(peek_current_char(ls))) {
        next_char(ls);
    }
    char *end;
    ls->number = cstr_tonumber(ls->lexeme.begin, &end);
    // If this is true, strtod failed to convert the entire token/lexeme.
    if (end != ls->lexeme.end) {
        lexerror_at_middle(ls, "Malformed number");
    }
    return make_token(ls, TK_NUMBER);
}

static Token string_token(Lexer *ls, char quote)
{
    while (peek_current_char(ls) != quote && !is_at_end(ls)) {
        if (peek_current_char(ls) == '\n') {
            goto bad_string;
        }
        next_char(ls);
    }

    // The label isn't actually in its own block scope.
    if (is_at_end(ls)) {
bad_string:
        lexerror_at_middle(ls, "Unterminated string");
    }

    // Consume closing quote.
    next_char(ls);

    // Left +1 to skip left quote, len -2 to get offset of last non-quote.
    StrView sv = make_strview(ls->lexeme.begin + 1, ls->lexeme.len - 2);
    ls->string = copy_string(ls->vm, sv);
    return make_token(ls, TK_STRING);
}

static Token rstring_token(Lexer *ls, int nesting)
{
    bool open = match_char(ls, '\n');
    if (open) {
        ls->line++;
    }
    multiline(ls, nesting);

    // Skip "[[" and '=' nesting, as well as "]]" and '=' - 2 for last index.
    int     mark = nesting + open + 2;
    int     len  = ls->lexeme.len - mark - 2;
    StrView sv   = make_strview(ls->lexeme.begin + mark, len);
    ls->string   = copy_rstring(ls->vm, sv);
    return make_token(ls, TK_STRING);
}

#define make_ifeq(lexer, ch, y, n) \
    make_token(lexer, match_char(lexer, ch) ? (y) : (n))

Token scan_token(Lexer *ls)
{
    skip_whitespace(ls);
    init_strview(&ls->lexeme, ls->lexeme.end);
    if (is_at_end(ls)) {
        return make_token(ls, TK_EOF);
    }

    char ch = next_char(ls);
    if (isdigit(ch)) {
        return number_token(ls);
    }
    if (isalpha(ch) || ch == '_') {
        return identifier_token(ls);
    }

    switch (ch) {
    case '(': return make_token(ls, TK_LPAREN);
    case ')': return make_token(ls, TK_RPAREN);
    case '[': {
        int lvl = get_nesting(ls);
        if (match_char(ls, '[')) {
            return rstring_token(ls, lvl);
        }
        return make_token(ls, TK_LBRACKET);
    }
    case ']': return make_token(ls, TK_RBRACKET);
    case '{': return make_token(ls, TK_LCURLY);
    case '}': return make_token(ls, TK_RCURLY);

    case ',': return make_token(ls, TK_COMMA);
    case ';': return make_token(ls, TK_SEMICOL);
    case '.':
        if (match_char(ls, '.')) {
            return make_ifeq(ls, '.', TK_VARARG, TK_CONCAT);
        } else if (isdigit(peek_current_char(ls))) {
            // Have a decimal literal with no leading digits, e.g. `.1`.
            return number_token(ls);
        } else {
            return make_token(ls, TK_PERIOD);
        }
    case '#': return make_token(ls, TK_POUND);

    case '+': return make_token(ls, TK_PLUS);
    case '-': return make_token(ls, TK_DASH);
    case '*': return make_token(ls, TK_STAR);
    case '/': return make_token(ls, TK_SLASH);
    case '%': return make_token(ls, TK_PERCENT);
    case '^': return make_token(ls, TK_CARET);

    case '=': return make_ifeq(ls, '=', TK_EQ, TK_ASSIGN);
    case '~':
        if (match_char(ls, '=')) {
            return make_token(ls, TK_NEQ);
        } else {
            lexerror_at_middle(ls, "Expected '='");
        }
    case '>': return make_ifeq(ls, '=', TK_GE, TK_GT);
    case '<': return make_ifeq(ls, '=', TK_LE, TK_LT);

    case '\"': return string_token(ls, '\"');
    case '\'': return string_token(ls, '\'');
    default:   return error_token(ls);
    }
}

void next_token(Lexer *ls)
{
    ls->consumed  = ls->lookahead;
    ls->lookahead = scan_token(ls);
    if (ls->lookahead.type == TK_ERROR) {
        lexerror_at_lookahead(ls, "Unexpected symbol");
    }
}

typedef struct {
    char  buffer[256];
    char *end;         // +1 past the last written character.
    int   left;        // How many free slots we can still write in.
    int   writes;      // How many slots we have written to so far.
} Builder;

static void init_builder(Builder *sb)
{
    sb->end    = sb->buffer;
    sb->left   = sizeof(sb->buffer);
    sb->writes = 0;
}

static void append_builder(Builder *sb, const char *fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);

    sb->writes = vsnprintf(sb->end, sb->left, fmt, argp);
    sb->end   += sb->writes;
    sb->left  -= sb->writes;
    *sb->end   = '\0';

    va_end(argp);
}

void expect_token(Lexer *ls, TkType type, const char *info)
{
    if (ls->lookahead.type == type) {
        next_token(ls);
        return;
    }

    Builder sb;
    StrView sv = LULU_TKINFO[type];

    init_builder(&sb);
    append_builder(&sb, "type '%s'", sv.begin);

    if (info != NULL) {
        append_builder(&sb, " %s", info);
    }

    lexerror_at_lookahead(ls, sb.buffer);
}

bool check_token(Lexer *ls, TkType type)
{
    TkType actual = ls->lookahead.type;
    return actual == type;
}

bool match_token(Lexer *ls, TkType type)
{
    if (check_token(ls, type)) {
        next_token(ls);
        return true;
    }
    return false;
}

#undef check_token_any
bool check_token_any(Lexer *ls, const TkType types[])
{
    for (int i = 0; types[i] != TK_ERROR; i++) {
        if (check_token(ls, types[i])) {
            return true;
        }
    }
    return false;
}

#undef match_token_any
bool match_token_any(Lexer *ls, const TkType types[])
{
    if (check_token_any(ls, types)) {
        next_token(ls);
        return true;
    }
    return false;
}


// 1}}} ------------------------------------------------------------------------

// ERROR HANDLING --------------------------------------------------------- {{{1

void lexerror_at(Lexer *ls, const Token *tk, const char *info)
{
    eprintf("%s:%i: %s", ls->name, ls->line, info);
    if (tk->type == TK_EOF) {
        eprintln(" at <eof>");
    } else {
        eprintfln(" near '%.*s'", tk->view.len, tk->view.begin);
    }
    longjmp(ls->vm->errorjmp, ERROR_COMPTIME);
}

void lexerror_at_lookahead(Lexer *ls, const char *info)
{
    lexerror_at(ls, &ls->lookahead, info);
}

void lexerror_at_consumed(Lexer *ls, const char *info)
{
    lexerror_at(ls, &ls->consumed, info);
}

void lexerror_at_middle(Lexer *ls, const char *info)
{
    Token token = error_token(ls);
    lexerror_at(ls, &token, info);
}


// 1}}} ------------------------------------------------------------------------
