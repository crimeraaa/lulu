#include <stdarg.h>
#include <ctype.h>
#include "api.h"
#include "lexer.h"
#include "limits.h"
#include "string.h"
#include "table.h"
#include "vm.h"

#define isident(ch)     (isalnum(ch) || (ch) == '_')

static const StringView LULU_TKINFO[] = {
    [TK_AND]      = sv_literal("and"),
    [TK_BREAK]    = sv_literal("break"),
    [TK_DO]       = sv_literal("do"),
    [TK_ELSE]     = sv_literal("else"),
    [TK_ELSEIF]   = sv_literal("elseif"),
    [TK_END]      = sv_literal("end"),
    [TK_FALSE]    = sv_literal("false"),
    [TK_FOR]      = sv_literal("for"),
    [TK_FUNCTION] = sv_literal("function"),
    [TK_IF]       = sv_literal("if"),
    [TK_IN]       = sv_literal("in"),
    [TK_LOCAL]    = sv_literal("local"),
    [TK_NIL]      = sv_literal("nil"),
    [TK_NOT]      = sv_literal("not"),
    [TK_OR]       = sv_literal("or"),
    [TK_PRINT]    = sv_literal("print"),
    [TK_RETURN]   = sv_literal("return"),
    [TK_THEN]     = sv_literal("then"),
    [TK_TRUE]     = sv_literal("true"),
    [TK_WHILE]    = sv_literal("while"),

    [TK_LPAREN]   = sv_literal("("),
    [TK_RPAREN]   = sv_literal(")"),
    [TK_LBRACKET] = sv_literal("["),
    [TK_RBRACKET] = sv_literal("]"),
    [TK_LCURLY]   = sv_literal("{"),
    [TK_RCURLY]   = sv_literal("}"),

    [TK_COMMA]    = sv_literal(","),
    [TK_SEMICOL]  = sv_literal(";"),
    [TK_VARARG]   = sv_literal("..."),
    [TK_CONCAT]   = sv_literal(".."),
    [TK_PERIOD]   = sv_literal("."),
    [TK_POUND]    = sv_literal("#"),

    [TK_PLUS]     = sv_literal("+"),
    [TK_DASH]     = sv_literal("-"),
    [TK_STAR]     = sv_literal("*"),
    [TK_SLASH]    = sv_literal("/"),
    [TK_PERCENT]  = sv_literal("%"),
    [TK_CARET]    = sv_literal("^"),

    [TK_ASSIGN]   = sv_literal("="),
    [TK_EQ]       = sv_literal("=="),
    [TK_NEQ]      = sv_literal("~="),
    [TK_GT]       = sv_literal(">"),
    [TK_GE]       = sv_literal(">="),
    [TK_LT]       = sv_literal("<"),
    [TK_LE]       = sv_literal("<="),

    [TK_IDENT]    = sv_literal("<identifier>"),
    [TK_STRING]   = sv_literal("<string>"),
    [TK_NUMBER]   = sv_literal("<number>"),
    [TK_ERROR]    = sv_literal("<error>"),
    [TK_EOF]      = sv_literal("<eof>"),
};

static void init_stringview(StringView *sv, const char *src)
{
    sv->begin = src;
    sv->end   = src;
    sv->len   = 0;
}

static void init_token(Token *tk)
{
    init_stringview(&tk->view, NULL);
    tk->line = 0;
    tk->type = TK_EOF;
}

void luluLex_init(Lexer *ls, const char *input, lulu_VM *vm)
{
    init_token(&ls->lookahead);
    init_token(&ls->consumed);
    init_stringview(&ls->lexeme, input);
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
    int lvl = 0;
    while (match_char(ls, '='))
        lvl++;
    return lvl;
}

// Consume a multi-line string or comment with a known nesting level.
static void multiline(Lexer *ls, int lvl)
{
    for (;;) {
        if (match_char(ls, ']')) {
            int close = get_nesting(ls);
            if (close == lvl && match_char(ls, ']'))
                return;
        }
        // Above call may have fallen through to here as well.
        if (is_at_end(ls)) {
            luluLex_error_middle(ls, "Unfinished multiline sequence");
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

static TkType check_keyword(TkType type, StringView word)
{
    StringView kw = LULU_TKINFO[type];
    if (kw.len == word.len && cstr_eq(kw.begin, word.begin, word.len)) {
        return type;
    }
    return TK_IDENT;
}

static TkType get_identifier_type(const Lexer *ls)
{
    StringView word = ls->lexeme;
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
    if (end != ls->lexeme.end)
        luluLex_error_middle(ls, "Malformed number");
    return make_token(ls, TK_NUMBER);
}

static Token string_token(Lexer *ls, char quote)
{
    while (peek_current_char(ls) != quote && !is_at_end(ls)) {
        if (peek_current_char(ls) == '\n')
            goto bad_string;
        next_char(ls);
    }

    // The label isn't actually in its own block scope.
    if (is_at_end(ls)) {
bad_string:
        luluLex_error_middle(ls, "Unterminated string");
    }

    // Consume closing quote.
    next_char(ls);

    const char *begin = ls->lexeme.begin + 1; // +1 to skip opening quote.
    const char *end   = ls->lexeme.end - 1;   // -1 to skip closing quote.
    StringView  sv    = sv_create_from_end(begin, end);
    ls->string = luluStr_copy(ls->vm, sv);
    return make_token(ls, TK_STRING);
}

static Token rstring_token(Lexer *ls, int lvl)
{
    bool open = match_char(ls, '\n');
    if (open) {
        ls->line++;
    }
    multiline(ls, lvl);

    int        mark = lvl + open + 2;            // Skip [[ and opening nesting.
    int        len  = ls->lexeme.len - mark - 2; // Offset of last non-quote.
    StringView sv   = sv_create_from_len(ls->lexeme.begin + mark, len);
    ls->string      = luluStr_copy_raw(ls->vm, sv);
    return make_token(ls, TK_STRING);
}

static Token make_token_ifelse(Lexer *ls, char expected, TkType y, TkType n)
{
    return make_token(ls, match_char(ls, expected) ? y : n);
}

Token luluLex_scan_token(Lexer *ls)
{
    skip_whitespace(ls);
    init_stringview(&ls->lexeme, ls->lexeme.end);
    if (is_at_end(ls))
        return make_token(ls, TK_EOF);

    char ch = next_char(ls);
    if (isdigit(ch))
        return number_token(ls);
    if (isalpha(ch) || ch == '_')
        return identifier_token(ls);

    switch (ch) {
    case '(': return make_token(ls, TK_LPAREN);
    case ')': return make_token(ls, TK_RPAREN);
    case '[': {
        int lvl = get_nesting(ls);
        if (match_char(ls, '['))
            return rstring_token(ls, lvl);
        return make_token(ls, TK_LBRACKET);
    }
    case ']': return make_token(ls, TK_RBRACKET);
    case '{': return make_token(ls, TK_LCURLY);
    case '}': return make_token(ls, TK_RCURLY);

    case ',': return make_token(ls, TK_COMMA);
    case ';': return make_token(ls, TK_SEMICOL);
    case '.':
        if (match_char(ls, '.'))
            return make_token_ifelse(ls, '.', TK_VARARG, TK_CONCAT);
        else if (isdigit(peek_current_char(ls)))
            return number_token(ls); // Decimal literal, no leading digits
        else
            return make_token(ls, TK_PERIOD);

    case '#': return make_token(ls, TK_POUND);
    case '+': return make_token(ls, TK_PLUS);
    case '-': return make_token(ls, TK_DASH);
    case '*': return make_token(ls, TK_STAR);
    case '/': return make_token(ls, TK_SLASH);
    case '%': return make_token(ls, TK_PERCENT);
    case '^': return make_token(ls, TK_CARET);
    case '=': return make_token_ifelse(ls, '=', TK_EQ, TK_ASSIGN);
    case '~':
        if (match_char(ls, '='))
            return make_token(ls, TK_NEQ);
        else
            luluLex_error_middle(ls, "Expected '='");

    case '>': return make_token_ifelse(ls, '=', TK_GE, TK_GT);
    case '<': return make_token_ifelse(ls, '=', TK_LE, TK_LT);

    case '\"': return string_token(ls, '\"');
    case '\'': return string_token(ls, '\'');
    default:   return error_token(ls);
    }
}

void luluLex_next_token(Lexer *ls)
{
    ls->consumed  = ls->lookahead;
    ls->lookahead = luluLex_scan_token(ls);
    if (ls->lookahead.type == TK_ERROR) {
        luluLex_error_lookahead(ls, "Unexpected symbol");
    }
}

typedef struct {
    char  buffer[256];
    char *end;         // +1 past the last written character.
    int   left;        // How many free slots we can still write in.
    int   writes;      // How many slots we have written to so far.
} StringBuilder;

static void init_stringbuilder(StringBuilder *sb)
{
    sb->end    = sb->buffer;
    sb->left   = sizeof(sb->buffer);
    sb->writes = 0;
}

static void append_stringbuilder(StringBuilder *sb, const char *fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    sb->writes = vsnprintf(sb->end, sb->left, fmt, argp);
    sb->end   += sb->writes;
    sb->left  -= sb->writes;
    *sb->end   = '\0';
    va_end(argp);
}

void luluLex_expect_token(Lexer *ls, TkType type, const char *info)
{
    if (ls->lookahead.type == type) {
        luluLex_next_token(ls);
        return;
    }
    StringBuilder sb;
    StringView    sv = LULU_TKINFO[type];
    init_stringbuilder(&sb);
    append_stringbuilder(&sb, "Expected '%s'", sv.begin);
    if (info != NULL)
        append_stringbuilder(&sb, " %s", info);
    luluLex_error_lookahead(ls, sb.buffer);
}

bool luluLex_check_token(Lexer *ls, TkType type)
{
    TkType actual = ls->lookahead.type;
    return actual == type;
}

bool luluLex_match_token(Lexer *ls, TkType type)
{
    if (luluLex_check_token(ls, type)) {
        luluLex_next_token(ls);
        return true;
    }
    return false;
}

#undef luluLex_check_token_any
bool luluLex_check_token_any(Lexer *ls, const TkType types[])
{
    for (int i = 0; types[i] != TK_ERROR; i++) {
        if (luluLex_check_token(ls, types[i]))
            return true;
    }
    return false;
}

#undef luluLex_match_token_any
bool luluLex_match_token_any(Lexer *ls, const TkType types[])
{
    if (luluLex_check_token_any(ls, types)) {
        luluLex_next_token(ls);
        return true;
    }
    return false;
}


// 1}}} ------------------------------------------------------------------------

// ERROR HANDLING --------------------------------------------------------- {{{1

void luluLex_error_at(Lexer *ls, const Token *tk, const char *info)
{
    lulu_VM *vm = ls->vm;
    if (tk->type == TK_EOF) {
        lulu_push_literal(vm, "at <eof>");
    } else {
        lulu_push_lcstring(vm, tk->view.begin, tk->view.len);
        lulu_push_fstring(vm, "near \'%s\'", lulu_to_cstring(vm, -1));
    }
    lulu_comptime_error(vm, ls->line, info, lulu_to_cstring(vm, -1));
}

void luluLex_error_lookahead(Lexer *ls, const char *info)
{
    luluLex_error_at(ls, &ls->lookahead, info);
}

void luluLex_error_consumed(Lexer *ls, const char *info)
{
    luluLex_error_at(ls, &ls->consumed, info);
}

void luluLex_error_middle(Lexer *ls, const char *info)
{
    Token tk = error_token(ls);
    luluLex_error_at(ls, &tk, info);
}


// 1}}} ------------------------------------------------------------------------
