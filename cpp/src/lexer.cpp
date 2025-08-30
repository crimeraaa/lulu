#include <stdio.h>
#include <stdlib.h>

#include "lexer.hpp"
#include "parser.hpp"
#include "compiler.hpp"
#include "vm.hpp"

static int
peek(const Lexer *x)
{
    return x->character;
}

static bool
is_eof(const Lexer *x)
{
    return x->character == STREAM_END;
}


// Returns the current character, discharging it and reading the next one.
static int
advance(Lexer *x)
{
    int ch       = x->character;
    x->character = x->stream->get_char();
    return ch;
}

Lexer
lexer_make(lulu_VM *L, OString *source, Stream *z, Builder *b)
{
    Lexer x{};
    x.L       = L;
    x.builder = b;
    x.source  = source;
    x.stream  = z;
    x.line    = 1;
    advance(&x);
    return x;
}

/**
 * @param ch
 *      The character to be appended to the buffer.
 */
static void
save(Lexer *x, int ch)
{
    // Negative values of char are undefined.
    lulu_assert(0 <= ch && ch <= CHAR_MAX);
    builder_write_char(x->L, x->builder, ch);
}

/**
 * @brief
 *      Write the current character to the buffer then discharge it,
 *      consuming the next character.
 */
static int
save_advance(Lexer *x)
{
    int ch = advance(x);
    save(x, ch);
    return ch;
}

static bool
check(const Lexer *x, char ch)
{
    return peek(x) == static_cast<int>(ch);
}

static bool
check2(const Lexer *x, char first, char second)
{
    return check(x, first) || check(x, second);
}

static bool
match(Lexer *x, char ch)
{
    bool found = check(x, ch);
    if (found) {
        advance(x);
    }
    return found;
}

static bool
match_save(Lexer *x, char ch)
{
    bool found = check(x, ch);
    if (found) {
        save_advance(x);
    }
    return found;
}

static bool
match2_save(Lexer *x, char first, char second)
{
    return match_save(x, first) || match_save(x, second);
}

static LString
get_lexeme(Lexer *x)
{
    return builder_to_string(*x->builder);
}

static LString
get_lexeme_nul_terminated(Lexer *x)
{
    builder_to_cstring(x->L, x->builder);
    return get_lexeme(x);
}

void
lexer_error(Lexer *x, Token_Type type, const char *what, int line)
{
    lulu_VM    *L = x->L;
    const char *where;
    switch (type) {
    // Only variable length tokens explicitly save to the buffer.
    case TOKEN_INVALID:
    case TOKEN_IDENT:
    case TOKEN_NUMBER:
    case TOKEN_STRING:
        where = builder_to_cstring(L, x->builder);
        break;
    default:
        where = token_cstring(type);
        break;
    }

    const char *source = x->source->to_cstring();
    vm_push_fstring(L, "%s:%i: %s near '%s'", source, line, what, where);
    vm_throw(L, LULU_ERROR_SYNTAX);
}


// Errors using the current lexeme as the error location.
[[noreturn]] static void
error(Lexer *x, const char *what)
{
    lexer_error(x, TOKEN_INVALID, what, x->line);
}

static void
expect(Lexer *x, char ch, const char *msg = nullptr)
{
    if (!match(x, ch)) {
        char buf[64];
        int  n = sprintf(buf, "Expected '%c'", ch);
        if (msg != nullptr) {
            sprintf(buf + n, " %s", msg);
        }
        error(x, buf);
    }
}

/**
 * @note 2025-06-12
 *  Assumptions:
 *  1.) Assumes we just consumed a '[' character.
 */
static int
get_nesting(Lexer *x, bool do_save)
{
    int        count      = 0;
    const auto advance_fn = (do_save) ? &save_advance : &advance;
    while (!is_eof(x) && check(x, '=')) {
        advance_fn(x);
        count++;
    }
    return count;
}

static void
skip_multiline(Lexer *x, int nest_open, bool do_save)
{
    const auto advance_fn = (do_save) ? &save_advance : &advance;
    for (;;) {
        if (is_eof(x)) {
            error(x, "Unterminated multiline sequence");
        }

        if (match(x, ']')) {
            // Don't save to buffer just yet; need to check if ending.
            int nest_close = get_nesting(x, /*do_save=*/false);
            if (match(x, ']') && nest_open == nest_close) {
                return;
            }

            // Not a nested comment, save if in multiline string literal.
            if (do_save) {
                save(x, ']');
                for (int i = 0; i < nest_close; i++) {
                    save(x, '=');
                }
                save(x, ']');
            }
            continue;
        }

        int ch = advance_fn(x);
        if (ch == '\n') {
            x->line++;
        }
    }
}

/**
 * @note 2025-06-12
 *  Assumptions:
 *  1.) We just consumed both '-' characters.
 *  2.) We are now pointing at the comment contents, '[', or a newline.
 */
static void
skip_comment(Lexer *x)
{
    // Multiline comment.
    if (match(x, '[')) {
        int nest_open = get_nesting(x, /*do_save=*/false);
        if (match(x, '[')) {
            skip_multiline(x, nest_open, /*do_save=*/false);
            return;
        }
        // If we didn't find the 2nd '[' then we fall back to single line.
    }
    // Single line
    while (!is_eof(x) && !check(x, '\n')) {
        advance(x);
    }
}


/**
 * @brief
 *      Continuously advances until we hit a non-whitespace and non-comment
 *      character.
 */
static void
skip_whitespace(Lexer *x)
{
    for (;;) {
        int ch = peek(x);
        switch (ch) {
        case '\n':
            x->line++;
            [[fallthrough]];
        case ' ':
        case '\r':
        case '\t':
            advance(x);
            break;
        default:
            return;
        }
    }
}

static bool
is_upper(int ch)
{
    return 'A' <= ch && ch <= 'Z';
}

static bool
is_lower(int ch)
{
    return 'a' <= ch && ch <= 'z';
}

static bool
is_number(int ch)
{
    return '0' <= ch && ch <= '9';
}

static bool
is_alpha(int ch)
{
    return is_upper(ch) || is_lower(ch) || ch == '_';
}

static bool
is_ident(int ch)
{
    return is_alpha(ch) || is_number(ch);
}

static void
consume_sequence(Lexer *x, bool (*predicate)(int ch))
{
    while (!is_eof(x) && predicate(peek(x))) {
        save_advance(x);
    }
}

/**
 * @note(2025-08-02)
 *  Assumptions:
 *
 *  1.) `advance()` was previously called so that `x->character != first`.
 */
static Token
make_number(Lexer *x, bool prefixed)
{
    if (prefixed) {
        // Save the prefix to the buffer for error reporting.
        int ch   = save_advance(x);
        int base = 0;
        switch (ch) {
        case 'B':
        case 'b': base = 2; break;
        case 'O':
        case 'o': base = 8; break;
        case 'D':
        case 'd': base = 10; break;
        case 'X':
        case 'x': base = 16; break;
        default:
            error(x, "Invalid integer prefix");
            break;
        }

        // Consume everything, don't check if it's a bad number yet.
        if (base != 0) {
            consume_sequence(x, is_ident);
            // Input must be nul-terminated to avoid UB in `strtoul()`.
            LString s = slice_from(get_lexeme_nul_terminated(x), 2);
            Number  d = 0;
            if (!lstring_to_number(s, &d, base)) {
                char buf[32];
                sprintf(buf, "Invalid base-%i integer", base);
                error(x, buf);
            }
            return Token::make(TOKEN_NUMBER, /*number=*/d);
        }
        // TODO(2025-06-12): Accept leading zeroes? Lua does, Python doesn't
    }

    // Consume '1.2.3'
    do {
        consume_sequence(x, is_number);
    } while (match_save(x, '.'));

    // Exponent form?
    if (match2_save(x, 'e', 'E')) {
        match2_save(x, '+', '-'); // optional sign
        consume_sequence(x, is_number);
    }
    consume_sequence(x, is_ident);

    // Input must be nul-terminated to avoid UB in `strtod()`.
    LString s = get_lexeme_nul_terminated(x);
    Number  d = 0;
    if (!lstring_to_number(s, &d)) {
        error(x, "Malformed number");
    }
    return Token::make(TOKEN_NUMBER, /*number=*/d);
}

static int
get_escaped(Lexer *x, char ch)
{
    switch (ch) {
    case '0':
        return '\0';
    case 'a':
        return '\a';
    case 'b':
        return '\b';
    case 'f':
        return '\f';
    case 'n':
        return '\n';
    case 't':
        return '\t';
    case 'r':
        return '\r';
    case 'v':
        return '\v';

    // Concept check:
    // `print("Hi\
    // mom!");`
    case '\n':
        x->line++;
    case '\'':
    case '\"':
    case '\\':
        return ch;
    default:
        break;
    }

    save(x, '\\');
    save(x, ch);
    error(x, "Invalid escape sequence");
}

OString *
lexer_new_ostring(lulu_VM *L, Lexer *x, LString ls)
{
    OString *s = ostring_new(L, ls);
    Table *t = x->indexes;
    Value k = Value::make_string(s);
    Value v;

    // If key exists, don't do anything as it's not collectible anyway.
    // Otherwise explicitly mark it to prevent collection.
    if (!s->is_fixed() && !table_get(t, k, &v)) {
        // Make sure `s` will not be collected because it's in a reachable
        // table that maps to non-nil.
        v = Value::make_boolean(true);
        vm_push_value(L, k);
        table_set(L, t, k, v);
        vm_pop_value(L);
        // luaC_checkGC(L);
    }
    return s;
}

static Token
make_string(Lexer *x, char q)
{
    lulu_VM *L = x->L;
    // Buffer should only contain the quote to be used in error messages.
    lulu_assert(builder_len(*x->builder) == 1);
    while (!is_eof(x) && !check2(x, q, '\n')) {
        int ch = advance(x);
        if (ch != '\\') {
            save(x, ch);
            continue;
        }

        // Read the character after '\'.
        ch = advance(x);
        ch = get_escaped(x, ch);
        save(x, ch);
    }
    expect(x, q, "to terminate string");

    // Skip the quote, which we initially saved to the buffer.
    LString  ls = slice_from(get_lexeme(x), 1);
    OString *os = lexer_new_ostring(L, x, ls);
    return Token::make_ostring(TOKEN_STRING, os);
}

Token
lexer_lex(Lexer *x)
{
    int        ch;
    Token_Type type;
    lulu_VM *L = x->L;

// This is only a hack for multiline comments.
lex_start:

    // Reset to ensure our buffer is clean for a fresh token.
    builder_reset(x->builder);
    skip_whitespace(x);
    if (is_eof(x)) {
        return Token::make(TOKEN_EOF);
    }

    ch = save_advance(x);
    if (is_alpha(ch)) {
        consume_sequence(x, is_ident);
        OString *os = lexer_new_ostring(L, x, get_lexeme(x));
        type        = static_cast<Token_Type>(os->keyword_type);
        if (type == TOKEN_INVALID) {
            type = TOKEN_IDENT;
        }
        return Token::make_ostring(type, os);
    } else if (is_number(ch)) {
        // '0' followed by some alphabetical character may be an integer prefix.
        bool prefixed = (ch == '0') && is_alpha(peek(x));
        return make_number(x, prefixed);
    }

    type = TOKEN_INVALID;
    switch (ch) {
    case '(': type = TOKEN_OPEN_PAREN; break;
    case ')': type = TOKEN_CLOSE_PAREN; break;
    case '{': type = TOKEN_OPEN_CURLY; break;
    case '}': type = TOKEN_CLOSE_CURLY; break;
    case '[':
        if (check2(x, '[', '=')) {
            int nest_open = get_nesting(x, /* do_save */ true);
            expect(x, '[', "to begin multiline string");
            save(x, '[');
            // Don't reset buffer by this point to aid in error reporting.
            skip_multiline(x, nest_open, /* do_save */ true);

            // Because we saved the opening, skip it here.
            LString  ls = slice_from(get_lexeme(x), nest_open + 2);
            OString *os = lexer_new_ostring(L, x, ls);
            return Token::make_ostring(TOKEN_STRING, os);
        }
        type = TOKEN_OPEN_BRACE;
        break;
    case ']': type = TOKEN_CLOSE_BRACE; break;
    case '+': type = TOKEN_PLUS; break;
    case '-':
        // We already advanced, so we have a second '-'?
        if (peek(x) == '-') {
            // Skip the second '-'.
            advance(x);
            skip_comment(x);

            /**
             * @note(2025-08-02)
             *      This is a bit of a hack because we have no other way
             *      of returning to the start of the function. Lua gets
             *      around this by using an infinite for loop and simply
             *      continuing or returning from it.
             *
             *      We could use a recursive call, but it could potentially
             *      overflow the stack if there are many subsequence
             *      multiline comments.
             */
            goto lex_start;
        }
        type = TOKEN_DASH;
        break;
    case '*': type = TOKEN_ASTERISK; break;
    case '/': type = TOKEN_SLASH; break;
    case '%': type = TOKEN_PERCENT; break;
    case '^': type = TOKEN_CARET; break;
    case '~': expect(x, '='); type = TOKEN_NOT_EQ; break;
    case '=': type = match(x, '=') ? TOKEN_EQ : TOKEN_ASSIGN; break;
    case '<': type = match(x, '=') ? TOKEN_LESS_EQ : TOKEN_LESS; break;
    case '>': type = match(x, '=') ? TOKEN_GREATER : TOKEN_GREATER_EQ; break;
    case '#': type = TOKEN_POUND; break;
    case '.':
        if (match(x, '.')) {
            type = match(x, '.') ? TOKEN_VARARG : TOKEN_CONCAT;
        } else {
            if (is_number(peek(x))) {
                // Leading radix points are never base-n integers.
                return make_number(x, /* prefixed */ false);
            }
            type = TOKEN_DOT;
        }
        break;
    case ',': type = TOKEN_COMMA; break;
    case ';': type = TOKEN_SEMI; break;
    case '\'':
    case '\"': return make_string(x, ch);
    }

    if (type == TOKEN_INVALID) {
        error(x, "Unexpected character");
    }
    return Token::make(type);
}

/**
 * @note 2025-06-14:
 *      ORDER: Keep in sync with `Token_Type`!
 */
const char *const token_strings[TOKEN_COUNT] = {
    // Keywords
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function",
    "if", "in", "local", "nil", "not", "or", "repeat", "return", "then",
    "true", "until", "while",

    // Balanced pairs
    "(", ")", "{", "}", "[", "]",

    // Arithmetic Operators
    "+", "-", "*", "/", "/", "^",

    // Relational Operators
    "==", "~=", "<", "<=", ">", ">=",

    // Misc.
    "#", ".", "..", "...", ",", ":", ";", "=", "<ident>", "<number>",
    "<string>", "<eof>",
};

static void
operator++(Token_Type &t, int)
{
    t = static_cast<Token_Type>(static_cast<int>(t) + 1);
}

void
lexer_global_init(lulu_VM *L)
{
    for (Token_Type t = TOKEN_AND; t <= TOKEN_WHILE; t++) {
        OString *s = ostring_new(L, lstring_from_cstring(token_cstring(t)));
        // All keywords are 'immortal'; they are never collected.
        s->set_fixed();
        s->keyword_type = t;
    }
}
