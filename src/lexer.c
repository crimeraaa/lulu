#include <stdarg.h>
#include <ctype.h>
#include "lexer.h"
#include "limits.h"
#include "string.h"
#include "table.h"
#include "vm.h"

static const LString LULU_TKINFO[] = {
    [TK_AND]      = lstr_from_lit("and"),
    [TK_BREAK]    = lstr_from_lit("break"),
    [TK_DO]       = lstr_from_lit("do"),
    [TK_ELSE]     = lstr_from_lit("else"),
    [TK_ELSEIF]   = lstr_from_lit("elseif"),
    [TK_END]      = lstr_from_lit("end"),
    [TK_FALSE]    = lstr_from_lit("false"),
    [TK_FOR]      = lstr_from_lit("for"),
    [TK_FUNCTION] = lstr_from_lit("function"),
    [TK_IF]       = lstr_from_lit("if"),
    [TK_IN]       = lstr_from_lit("in"),
    [TK_LOCAL]    = lstr_from_lit("local"),
    [TK_NIL]      = lstr_from_lit("nil"),
    [TK_NOT]      = lstr_from_lit("not"),
    [TK_OR]       = lstr_from_lit("or"),
    [TK_PRINT]    = lstr_from_lit("print"),
    [TK_RETURN]   = lstr_from_lit("return"),
    [TK_THEN]     = lstr_from_lit("then"),
    [TK_TRUE]     = lstr_from_lit("true"),
    [TK_WHILE]    = lstr_from_lit("while"),

    [TK_LPAREN]   = lstr_from_lit("("),
    [TK_RPAREN]   = lstr_from_lit(")"),
    [TK_LBRACKET] = lstr_from_lit("["),
    [TK_RBRACKET] = lstr_from_lit("]"),
    [TK_LCURLY]   = lstr_from_lit("{"),
    [TK_RCURLY]   = lstr_from_lit("}"),

    [TK_COMMA]    = lstr_from_lit(","),
    [TK_SEMICOL]  = lstr_from_lit(";"),
    [TK_VARARG]   = lstr_from_lit("..."),
    [TK_CONCAT]   = lstr_from_lit(".."),
    [TK_PERIOD]   = lstr_from_lit("."),
    [TK_POUND]    = lstr_from_lit("#"),

    [TK_PLUS]     = lstr_from_lit("+"),
    [TK_DASH]     = lstr_from_lit("-"),
    [TK_STAR]     = lstr_from_lit("*"),
    [TK_SLASH]    = lstr_from_lit("/"),
    [TK_PERCENT]  = lstr_from_lit("%"),
    [TK_CARET]    = lstr_from_lit("^"),

    [TK_ASSIGN]   = lstr_from_lit("="),
    [TK_EQ]       = lstr_from_lit("=="),
    [TK_NEQ]      = lstr_from_lit("~="),
    [TK_GT]       = lstr_from_lit(">"),
    [TK_GE]       = lstr_from_lit(">="),
    [TK_LT]       = lstr_from_lit("<"),
    [TK_LE]       = lstr_from_lit("<="),

    [TK_IDENT]    = lstr_from_lit("<identifier>"),
    [TK_STRING]   = lstr_from_lit("<string>"),
    [TK_NUMBER]   = lstr_from_lit("<number>"),
    [TK_ERROR]    = lstr_from_lit("<error>"),
    [TK_EOF]      = lstr_from_lit("<eof>"),
};

static const char *to_string(TkType type)
{
    return LULU_TKINFO[type].string;
}

void luluLex_intern_tokens(lulu_VM *vm)
{
    for (size_t i = 0; i < array_len(LULU_TKINFO); i++) {
        LString s = LULU_TKINFO[i];
        luluStr_copy(vm, s.string, s.length);
    }
}

// HELPERS ---------------------------------------------------------------- {{{1

// Analogous to `scanner.c:advance()` in the book.
// Returns the character being pointed to before the advancement.
static char next_char(Lexer *ls)
{
    ls->current = luluZIO_getc_stream(ls->stream);
    return ls->current;
}

static char current_char(const Lexer *ls)
{
    return ls->current;
}

static char lookahead_char(const Lexer *ls)
{
    return luluZIO_lookahead_stream(ls->stream);
}

static bool is_eof(const Lexer *ls)
{
    return current_char(ls) == '\0';
}

static bool is_ident(const Lexer *ls)
{
    return isalnum(current_char(ls)) || current_char(ls) == '_';
}

// Append `ch` to the buffer.
static void save_char(Lexer *ls, char ch)
{
    Buffer *b = ls->buffer;
    if (b->length + 1 > b->capacity)
        luluZIO_resize_buffer(ls->vm, b, luluMem_grow_capacity(b->capacity));
    b->buffer[b->length] = ch;
    b->length += 1;
}

// Save `ls->current` to the buffer then advance, returning the newly read character.
static char save_and_next(Lexer *ls)
{
    save_char(ls, ls->current);
    return next_char(ls);
}

static bool check_char(Lexer *ls, char ch)
{
    return !is_eof(ls) && current_char(ls) == ch;
}

// If matches, save `ls->current` to the buffer and advance.
static bool match_char(Lexer *ls, char ch)
{
    bool found = check_char(ls, ch);
    if (found)
        save_and_next(ls);
    return found;
}

static bool skip_char_if(Lexer *ls, char ch)
{
    bool found = check_char(ls, ch);
    if (found)
        next_char(ls);
    return found;
}

static bool check_char_any(Lexer *ls, const char *set)
{
    return strchr(set, current_char(ls)) != nullptr;
}

static bool match_char_any(Lexer *ls, const char *set)
{
    bool found = check_char_any(ls, set);
    if (found)
        save_and_next(ls);
    return found;
}

// TODO: Differentiate CR, LF, CRLF?
static bool is_newline(Lexer *ls)
{
    return check_char_any(ls, "\r\n");
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
        if (is_eof(ls)) {
            luluLex_error_middle(ls, "Unfinished multiline sequence");
            return;
        }
        // Think of this as the iterator increment.
        if (is_newline(ls))
            ls->line += 1;
        save_and_next(ls);
    }
}

static void skip_comment(Lexer *ls)
{
    // Look for the first '[' which starts off a multiline string/comment.
    if (match_char(ls, '[')) {
        int lvl = get_nesting(ls);
        if (match_char(ls, '[')) {
            multiline(ls, lvl);
            luluZIO_reset_buffer(ls->buffer);
            return;
        }
        // Undo buffer writes because scan_token() still has work to do.
        luluZIO_reset_buffer(ls->buffer);
    }
    // Didn't match 2 '['. Fall through case.
    while (!is_eof(ls) && !is_newline(ls)) {
        next_char(ls);
    }
}

// 1}}} ------------------------------------------------------------------------

static void init_token(Token *tk)
{
    tk->line = 0;
    tk->type = TK_EOF;
    tk->data.string = nullptr;
}

void luluLex_init(lulu_VM *vm, Lexer *ls, Stream *z, Buffer *b)
{
    init_token(&ls->lookahead);
    init_token(&ls->consumed);
    ls->stream = z;
    ls->buffer = b;
    ls->vm     = vm;
    ls->line   = 1;
    next_char(ls);
}


// TOKENIZER -------------------------------------------------------------- {{{1

static Token make_token(Lexer *ls, TkType type)
{
    Token tk;
    tk.type = type;
    tk.line = ls->line;
    tk.data.string = nullptr; // Mark as unavailable until known otherwise.
    tk.data.number = 0;
    return tk;
}

static Token error_token(Lexer *ls)
{
    Buffer *b = ls->buffer;

    // For error tokens, report only the first line if this is a multiline.
    const char *end   = b->buffer + b->length;
    const char *newl  = memchr(b->buffer, '\n', b->length);
    if (newl != nullptr)
        end = newl;

    Token t = make_token(ls, TK_ERROR);
    t.data.string = luluStr_copy(ls->vm, b->buffer, end - b->buffer);
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

static TkType check_keyword(TkType type, LString w)
{
    LString k = LULU_TKINFO[type];
    if (k.length == w.length && cstr_eq(k.string, w.string, w.length))
        return type;
    return TK_IDENT;
}

static TkType get_identifier_type(const Lexer *ls)
{
    Buffer *b = ls->buffer;
    LString w = {b->buffer, b->length};
    switch (w.string[0]) {
    case 'a': return check_keyword(TK_AND, w);
    case 'b': return check_keyword(TK_BREAK, w);
    case 'd': return check_keyword(TK_DO, w);
    case 'e':
        switch (w.length) {
        case cstr_len("end"):
            return check_keyword(TK_END, w);
        case cstr_len("else"):
            return check_keyword(TK_ELSE, w);
        case cstr_len("elseif"):
            return check_keyword(TK_ELSEIF, w);
        }
        break;
    case 'f':
        if (w.length > 1) {
            switch (w.string[1]) {
            case 'a': return check_keyword(TK_FALSE, w);
            case 'o': return check_keyword(TK_FOR, w);
            case 'u': return check_keyword(TK_FUNCTION, w);
            }
        }
        break;
    case 'i':
        if (w.length == cstr_len("if")) {
            switch (w.string[1]) {
            case 'f': return check_keyword(TK_IF, w);
            case 'n': return check_keyword(TK_IN, w);
            }
        }
        break;
    case 'l': return check_keyword(TK_LOCAL, w);
    case 'n':
        if (w.length == cstr_len("nil")) {
            switch (w.string[1]) {
            case 'i': return check_keyword(TK_NIL, w);
            case 'o': return check_keyword(TK_NOT, w);
            }
        }
        break;
    case 'o': return check_keyword(TK_OR, w);
    case 'p': return check_keyword(TK_PRINT, w);
    case 'r': return check_keyword(TK_RETURN, w);
    case 't':
        if (w.length == cstr_len("true")) {
            switch (w.string[1]) {
            case 'h': return check_keyword(TK_THEN, w);
            case 'r': return check_keyword(TK_TRUE, w);
            }
        }
        break;
    case 'w': return check_keyword(TK_WHILE, w);
    }
    return TK_IDENT;
}

static Token identifier_token(Lexer *ls)
{
    while (is_ident(ls)) {
        save_and_next(ls);
    }
    Buffer *b = ls->buffer;
    Token   t = make_token(ls, get_identifier_type(ls));
    t.data.string = luluStr_copy(ls->vm, b->buffer, b->length);
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
static void consume_number(Lexer *ls)
{
    for (;;) {
        while (isdigit(current_char(ls))) {
            save_and_next(ls);
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
    consume_number(ls);
    while (is_ident(ls)) {
        save_and_next(ls);
    }
    Token   t   = make_token(ls, TK_NUMBER);
    Buffer *b   = ls->buffer;
    String *s   = luluStr_copy(ls->vm, b->buffer, b->length);
    char   *end = nullptr;

    // Pass `s->data` not `v.begin`, because `strtod` may read out of bounds.
    Number n = cstr_tonumber(s->data, &end);
    t.data.string = s;
    t.data.number = n;

    // Failed to convert entire lexeme? (see `man strtod` if using that)
    if (end != s->data + s->len)
        luluLex_error_middle(ls, "Malformed number");
    return t;
}

static Token copy_string(Lexer *ls, int offset)
{
    Buffer     *b     = ls->buffer;
    const char *begin = b->buffer + offset;
    const char *end   = b->buffer + b->length - offset;

    Token t = make_token(ls, TK_STRING);
    t.data.string = luluStr_copy(ls->vm, begin, end - begin);
    return t;
}

static Token string_token(Lexer *ls, char quote)
{
    // Keep going until closing quote has been consumed.
    while (!match_char(ls, quote)) {
        if (current_char(ls) == '\\') {
            char ch = '\0';
            // Skip slash w/o saving to buffer.
            switch (next_char(ls)) {
            case '0': ch = '\0'; break;
            case 'a': ch = '\a'; break;
            case 'b': ch = '\b'; break;
            case 'f': ch = '\f'; break;
            case 'n': ch = '\n'; break;
            case 'r': ch = '\r'; break;
            case 't': ch = '\t'; break;
            case 'v': ch = '\v'; break;
            default:
                if (check_char_any(ls, "\?\'\"\\")) {
                    ch = current_char(ls);
                    break;
                }
                // Since we skipped the slash need to explicitly prepend it.
                luluZIO_reset_buffer(ls->buffer);
                save_char(ls, '\\');
                save_and_next(ls);
                luluLex_error_middle(ls, "Unsupported escape sequence");
                break;
            }
            save_char(ls, ch);
            next_char(ls);
        } else if (match_char_any(ls, "\r\n") || is_eof(ls)) {
            luluLex_error_middle(ls, "Unterminated string");
        } else {
            save_and_next(ls);
        }
    }
    // +1 offset to skip both quotes.
    return copy_string(ls, +1);
}

static Token long_string_token(Lexer *ls, int lvl)
{
    // Opening newlines shouldn't be counted in the buffer.
    bool open = skip_char_if(ls, '\n');
    if (open)
        ls->line++;
    multiline(ls, lvl);

    int mark = lvl + open + 2; // Skip [[ and opening nesting.
    return copy_string(ls, mark);
}

static Token make_token_ifelse(Lexer *ls, char expected, TkType y, TkType n)
{
    return make_token(ls, match_char(ls, expected) ? y : n);
}

Token luluLex_scan_token(Lexer *ls)
{
    // Always start with index 0 for a new token.
    luluZIO_reset_buffer(ls->buffer);
    skip_whitespace(ls);
    if (is_eof(ls))
        return make_token(ls, TK_EOF);

    char ch = current_char(ls);
    if (isdigit(ch))
        return number_token(ls);
    if (isalpha(ch) || ch == '_')
        return identifier_token(ls);

    save_and_next(ls);
    switch (ch) {
    case '(': return make_token(ls, TK_LPAREN);
    case ')': return make_token(ls, TK_RPAREN);
    case '[': {
        int lvl = get_nesting(ls);
        if (match_char(ls, '['))
            return long_string_token(ls, lvl);
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

    case '\"':
    case '\'': return string_token(ls, ch);
    default:   return error_token(ls);
    }
}

void luluLex_next_token(Lexer *ls)
{
    ls->consumed  = ls->lookahead;
    ls->lookahead = luluLex_scan_token(ls);
    if (ls->lookahead.type == TK_ERROR)
        luluLex_error_lookahead(ls, "Unexpected symbol");
}

void luluLex_expect_token(Lexer *ls, TkType type, const char *info)
{
    if (ls->lookahead.type == type) {
        luluLex_next_token(ls);
        return;
    }
    lulu_VM    *vm  = ls->vm;
    const char *msg = lulu_push_fstring(vm, "Expected '%s'", to_string(type));
    if (info != nullptr) {
        lulu_push_fstring(vm, " %s", info);
        msg = lulu_concat(vm, 2);
    }
    lulu_pop(vm, 1);
    luluLex_error_lookahead(ls, msg);
}

bool luluLex_check_token(Lexer *ls, TkType type)
{
    return ls->lookahead.type == type;
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
    lulu_VM *vm   = ls->vm;
    String  *s    = tk->data.string;
    TkType   type = tk->type;
    if (type == TK_EOF)
        lulu_push_literal(vm, "at ");
    else
        lulu_push_literal(vm, "near ");
    lulu_push_fstring(vm, "\'%s\'", (s != nullptr) ? s->data : to_string(type));
    const char *msg = lulu_concat(vm, 2);
    lulu_pop(vm, 1);
    lulu_comptime_error(vm, ls->line, info, msg);
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
