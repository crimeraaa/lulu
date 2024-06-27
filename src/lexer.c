#include <stdarg.h>
#include <ctype.h>
#include "api.h"
#include "lexer.h"
#include "limits.h"
#include "string.h"
#include "table.h"
#include "vm.h"

#define isident(ch)     (isalnum(ch) || (ch) == '_')

static const View LULU_TKINFO[] = {
    [TK_AND]      = view_from_lit("and"),
    [TK_BREAK]    = view_from_lit("break"),
    [TK_DO]       = view_from_lit("do"),
    [TK_ELSE]     = view_from_lit("else"),
    [TK_ELSEIF]   = view_from_lit("elseif"),
    [TK_END]      = view_from_lit("end"),
    [TK_FALSE]    = view_from_lit("false"),
    [TK_FOR]      = view_from_lit("for"),
    [TK_FUNCTION] = view_from_lit("function"),
    [TK_IF]       = view_from_lit("if"),
    [TK_IN]       = view_from_lit("in"),
    [TK_LOCAL]    = view_from_lit("local"),
    [TK_NIL]      = view_from_lit("nil"),
    [TK_NOT]      = view_from_lit("not"),
    [TK_OR]       = view_from_lit("or"),
    [TK_PRINT]    = view_from_lit("print"),
    [TK_RETURN]   = view_from_lit("return"),
    [TK_THEN]     = view_from_lit("then"),
    [TK_TRUE]     = view_from_lit("true"),
    [TK_WHILE]    = view_from_lit("while"),

    [TK_LPAREN]   = view_from_lit("("),
    [TK_RPAREN]   = view_from_lit(")"),
    [TK_LBRACKET] = view_from_lit("["),
    [TK_RBRACKET] = view_from_lit("]"),
    [TK_LCURLY]   = view_from_lit("{"),
    [TK_RCURLY]   = view_from_lit("}"),

    [TK_COMMA]    = view_from_lit(","),
    [TK_SEMICOL]  = view_from_lit(";"),
    [TK_VARARG]   = view_from_lit("..."),
    [TK_CONCAT]   = view_from_lit(".."),
    [TK_PERIOD]   = view_from_lit("."),
    [TK_POUND]    = view_from_lit("#"),

    [TK_PLUS]     = view_from_lit("+"),
    [TK_DASH]     = view_from_lit("-"),
    [TK_STAR]     = view_from_lit("*"),
    [TK_SLASH]    = view_from_lit("/"),
    [TK_PERCENT]  = view_from_lit("%"),
    [TK_CARET]    = view_from_lit("^"),

    [TK_ASSIGN]   = view_from_lit("="),
    [TK_EQ]       = view_from_lit("=="),
    [TK_NEQ]      = view_from_lit("~="),
    [TK_GT]       = view_from_lit(">"),
    [TK_GE]       = view_from_lit(">="),
    [TK_LT]       = view_from_lit("<"),
    [TK_LE]       = view_from_lit("<="),

    [TK_IDENT]    = view_from_lit("<identifier>"),
    [TK_STRING]   = view_from_lit("<string>"),
    [TK_NUMBER]   = view_from_lit("<number>"),
    [TK_ERROR]    = view_from_lit("<error>"),
    [TK_EOF]      = view_from_lit("<eof>"),
};

const char *luluLex_token_to_string(TkType type)
{
    return LULU_TKINFO[type].begin;
}

void luluLex_intern_tokens(lulu_VM *vm)
{
    for (size_t i = 0; i < array_len(LULU_TKINFO); i++) {
        String *s = luluStr_copy(vm, LULU_TKINFO[i]);
        luluStr_set_interned(vm, s);
    }
}

static void init_view(View *sv, const char *src)
{
    sv->begin = src;
    sv->end   = src;
}

static void init_token(Token *tk)
{
    tk->line = 0;
    tk->type = TK_EOF;
    tk->data.string = NULL;
}

void luluLex_init(Lexer *ls, const char *input, lulu_VM *vm)
{
    init_token(&ls->lookahead);
    init_token(&ls->consumed);
    init_view(&ls->lexeme, input);
    ls->vm   = vm;
    ls->line = 1;
}

// HELPERS ---------------------------------------------------------------- {{{1

static bool is_at_end(const Lexer *ls)
{
    return ls->lexeme.end[0] == '\0';
}

static char current_char(const Lexer *ls)
{
    return ls->lexeme.end[0];
}

static char lookahead_char(const Lexer *ls)
{
    return ls->lexeme.end[1];
}

// Analogous to `scanner.c:advance()` in the book.
static char next_char(Lexer *ls)
{
    ls->lexeme.end += 1;
    return ls->lexeme.end[-1];
}

static bool match_char(Lexer *ls, char ch)
{
    if (is_at_end(ls) || current_char(ls) != ch) {
        return false;
    } else {
        next_char(ls);
        return true;
    }
}

static bool check_char_any(Lexer *ls, const char *set)
{
    return strchr(set, current_char(ls)) != NULL;
}

static bool match_char_any(Lexer *ls, const char *set)
{
    bool found = check_char_any(ls, set);
    if (found)
        next_char(ls);
    return found;
}

static void singleline(Lexer *ls)
{
    while (current_char(ls) != '\n' && !is_at_end(ls)) {
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
        if (next_char(ls) == '\n')
            ls->line += 1;
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
    tk.type = type;
    tk.line = ls->line;
    return tk;
}

static Token error_token(Lexer *ls)
{
    // For error tokens, report only the first line if this is a multiline.
    const char *endl = memchr(ls->lexeme.begin, '\n', view_len(ls->lexeme));
    if (endl != NULL)
        ls->lexeme.end = endl;

    Token t = make_token(ls, TK_ERROR);
    t.data.string = luluStr_copy(ls->vm, ls->lexeme);
    return t;
}

static void skip_whitespace(Lexer *ls)
{
    for (;;) {
        char ch = current_char(ls);
        switch (ch) {
        case '\n': ls->line += 1; // fall through
        case ' ':
        case '\r':
        case '\t': next_char(ls); break;
        case '-':
            if (lookahead_char(ls) != '-')
                return;
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

static TkType check_keyword(TkType type, View word)
{
    View   kw = LULU_TKINFO[type];
    size_t l1 = view_len(kw);
    size_t l2 = view_len(word);
    if (l1 == l2 && cstr_eq(kw.begin, word.begin, l2))
        return type;
    return TK_IDENT;
}

static TkType get_identifier_type(const Lexer *ls)
{
    View   word = ls->lexeme;
    size_t len  = view_len(word);
    switch (word.begin[0]) {
    case 'a': return check_keyword(TK_AND, word);
    case 'b': return check_keyword(TK_BREAK, word);
    case 'd': return check_keyword(TK_DO, word);
    case 'e':
        switch (len) {
        case cstr_len("end"):
            return check_keyword(TK_END, word);
        case cstr_len("else"):
            return check_keyword(TK_ELSE, word);
        case cstr_len("elseif"):
            return check_keyword(TK_ELSEIF, word);
        }
        break;
    case 'f':
        if (len > 1) {
            switch (word.begin[1]) {
            case 'a': return check_keyword(TK_FALSE, word);
            case 'o': return check_keyword(TK_FOR, word);
            case 'u': return check_keyword(TK_FUNCTION, word);
            }
        }
        break;
    case 'i':
        if (len == cstr_len("if")) {
            switch (word.begin[1]) {
            case 'f': return check_keyword(TK_IF, word);
            case 'n': return check_keyword(TK_IN, word);
            }
        }
        break;
    case 'l': return check_keyword(TK_LOCAL, word);
    case 'n':
        if (len == cstr_len("nil")) {
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
        if (len == cstr_len("true")) {
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
    while (isident(current_char(ls))) {
        next_char(ls);
    }
    Token t = make_token(ls, get_identifier_type(ls));
    t.data.string = luluStr_copy(ls->vm, ls->lexeme);
    return t;
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
    for (;;) {
        while (isdigit(current_char(ls))) {
            next_char(ls);
        }

        // Have an exponent? It CANNOT come after the decimal point.
        if (match_char_any(ls, "Ee")) {
            match_char_any(ls, "+-"); // Explicit signedness is optional.
            continue;
        }

        // Have a fraction? This sequence of digits can also have an exponent.
        if (match_char(ls, '.')) {
            continue;
        }
        break;
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
        while (isxdigit(current_char(ls))) {
            next_char(ls);
        }
    } else {
        decimal_sequence(ls);
    }
    // Consume any trailing characters for error handling later on.
    while (isident(current_char(ls))) {
        next_char(ls);
    }
    char  *end = NULL;
    Token  tk  = make_token(ls, TK_NUMBER);
    Number n   = cstr_tonumber(ls->lexeme.begin, &end);

    // Intern string representation, used when reporting errors.
    tk.data.string = luluStr_copy(ls->vm, ls->lexeme);
    // Failed to convert entire lexeme? (see `man strtod` if using that)
    if (end != ls->lexeme.end)
        luluLex_error_middle(ls, "Malformed number");

    tk.data.number = n;
    return tk;
}

static Token copy_string(Lexer *ls, int offset, bool is_raw)
{
    const char *begin = ls->lexeme.begin + offset;
    const char *end   = ls->lexeme.end - offset;
    lulu_VM    *vm    = ls->vm;

    View  sv = view_from_end(begin, end);
    Token tk = make_token(ls, TK_STRING);
    tk.data.string = (is_raw) ? luluStr_copy_raw(vm, sv) : luluStr_copy(vm, sv);
    return tk;
}

static Token string_token(Lexer *ls, char quote)
{
    while (current_char(ls) != quote) {
        if (current_char(ls) == '\n' || is_at_end(ls))
            luluLex_error_middle(ls, "Unterminated string");
        next_char(ls);
    }

    // Consume closing quote.
    next_char(ls);

    // +1 offset to skip both quotes.
    return copy_string(ls, +1, false);
}

static Token rstring_token(Lexer *ls, int lvl)
{
    bool open = match_char(ls, '\n');
    if (open) {
        ls->line++;
    }
    multiline(ls, lvl);

    int mark = lvl + open + 2; // Skip [[ and opening nesting.
    return copy_string(ls, mark, true);
}

static Token make_token_ifelse(Lexer *ls, char expected, TkType y, TkType n)
{
    return make_token(ls, match_char(ls, expected) ? y : n);
}

Token luluLex_scan_token(Lexer *ls)
{
    skip_whitespace(ls);
    init_view(&ls->lexeme, ls->lexeme.end);
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
        else if (isdigit(current_char(ls)))
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

void luluLex_expect_token(Lexer *ls, TkType type, const char *info)
{
    if (ls->lookahead.type == type) {
        luluLex_next_token(ls);
        return;
    }
    Builder sb;
    init_builder(&sb);
    append_builder(&sb, "Expected '%s'", luluLex_token_to_string(type));
    if (info != NULL)
        append_builder(&sb, " %s", info);
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

// 1}}} ------------------------------------------------------------------------

// ERROR HANDLING --------------------------------------------------------- {{{1

void luluLex_error_at(Lexer *ls, const Token *tk, const char *info)
{
    lulu_VM *vm = ls->vm;
    switch (tk->type) {
    case TK_EOF:
        lulu_push_literal(vm, "at <eof>");
        break;
    case TK_IDENT:
    case TK_STRING:
    case TK_NUMBER: // When errors thrown, data.string is active.
    case TK_ERROR:
        lulu_push_fstring(vm, "near \'%s\'", tk->data.string->data);
        break;
    default:
        lulu_push_fstring(vm, "near \'%s\'", LULU_TKINFO[tk->type].begin);
        break;
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
