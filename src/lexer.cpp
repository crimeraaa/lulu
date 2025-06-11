#include "lexer.hpp"

Lexer
lexer_make(String source, String script)
{
    Lexer l;
    l.source  = source;
    l.script  = script;
    l.start   = 0;
    l.cursor  = 0;
    l.line    = 1;
    return l;
}

static bool
is_eof(const Lexer &x)
{
    return x.cursor >= len(x.script);
}

static char
peek(const Lexer &x)
{
    return x.script[x.cursor];
}

static char
peek_next(const Lexer &x)
{
    size_t i = x.cursor + 1;
    if (i < len(x.script)) {
        return x.script[i];
    }
    return '\0';
}

static char
advance(Lexer &x)
{
    char prev = peek(x);
    x.cursor++;
    return prev;
}

static bool
check(const Lexer &x, char ch)
{
    return peek(x) == ch;
}

static bool
match(Lexer &x, char ch)
{
    bool found = check(x, ch);
    if (found) {
        advance(x);
    }
    return found;
}

static void
skip_whitespace(Lexer &x)
{
    // On the first iteration, if we're already at eof, `peek()` would throw.
    // This is because we're using indexes and not pointers. Although we can
    // hold pointers to 1 past the last element, we cannot index it.
    if (is_eof(x)) {
        return;
    }

    for (;;) {
        char ch = peek(x);
        switch (ch) {
        case '\n': x.line++; // fall-through
        case ' ':
        case '\r':
        case '\t':
            advance(x);
            break;
        case '-':
            if (peek_next(x) != '-') {
                return;
            }
            while (!is_eof(x) && !check(x, '\n')) {
                advance(x);
            }
            break;
        default:
            return;
        }
    }
}

static bool
is_upper(char ch)
{
    return 'A' <= ch && ch <= 'Z';
}

static bool
is_lower(char ch)
{
    return 'a' <= ch && ch <= 'z';
}

static bool
is_digit(char ch)
{
    return '0' <= ch && ch <= '9';
}

static bool
is_alpha(char ch)
{
    return is_upper(ch) || is_lower(ch) || ch == '_';
}

static bool
is_ident(char ch)
{
    return is_alpha(ch) || is_digit(ch);
}

static Token_Type
get_type(Lexer &x, char ch)
{
    switch (ch) {
    case '(': return TOKEN_OPEN_PAREN;
    case ')': return TOKEN_CLOSE_PAREN;
    case '{': return TOKEN_OPEN_CURLY;
    case '}': return TOKEN_CLOSE_CURLY;
    case '[': return TOKEN_OPEN_BRACE;
    case ']': return TOKEN_CLOSE_BRACE;

    case '+': return TOKEN_PLUS;
    case '-': return TOKEN_DASH;
    case '*': return TOKEN_ASTERISK;
    case '/': return TOKEN_SLASH;
    case '%': return TOKEN_PERCENT;
    case '^': return TOKEN_CARET;

    case '=': return match(x, '=') ? TOKEN_EQ : TOKEN_ASSIGN;
    case '<': return match(x, '=') ? TOKEN_LESS_EQ : TOKEN_LESS;
    case '>': return match(x, '=') ? TOKEN_GREATER : TOKEN_GREATER_EQ;

    case '.':
        if (match(x, '.')) {
            return match(x, '.') ? TOKEN_VARARG : TOKEN_CONCAT;
        }
        // TODO(2025-06-12): check for numbers
        return TOKEN_DOT;
    case ',': return TOKEN_COMMA;
    case ';': return TOKEN_SEMI;

    case '\'':
    case '\"':
        while (!match(x, ch)) {
            advance(x);
        }
        return TOKEN_STRING;
    }

    return TOKEN_INVALID;
}

static Token
make_token(Lexer &x, Token_Type type)
{
    Token t{string_slice(x.script, x.start, x.cursor), type, x.line};
    return t;
}

static Token
make_eof(Lexer &x)
{
    return {string_slice(x.script, 0, 0), TOKEN_EOF, x.line};
}

static void
consume_sequence(Lexer &x, bool (*predicate)(char ch))
{
    while (!is_eof(x) && predicate(peek(x))) {
        advance(x);
    }
}

Token
lexer_lex(Lexer &x)
{
    skip_whitespace(x);
    x.start = x.cursor;
    if (is_eof(x)) {
        return make_eof(x);
    }

    char ch = advance(x);
    if (is_alpha(ch)) {
        consume_sequence(x, is_ident);
        return make_token(x, TOKEN_IDENTIFIER);
    } else if (is_digit(ch)) {
        consume_sequence(x, is_digit);
        if (peek(x) == '.') {
            consume_sequence(x, is_digit);
        }
        return make_token(x, TOKEN_NUMBER);
    }

    Token_Type type = get_type(x, ch);
    return make_token(x, type);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wc99-designator"

#define STR(s)  {s, sizeof(s) - 1}

const String token_strings[TOKEN_COUNT] = {
    [TOKEN_INVALID]     = STR("<invalid>"),

    // Keywords
    [TOKEN_AND]         = STR("and"),       [TOKEN_BREAK]       = STR("break"),
    [TOKEN_DO]          = STR("do"),        [TOKEN_ELSE]        = STR("else"),
    [TOKEN_ELSEIF]      = STR("elseif"),    [TOKEN_END]         = STR("end"),
    [TOKEN_FALSE]       = STR("false"),     [TOKEN_FOR]         = STR("for"),
    [TOKEN_FUNCTION]    = STR("function"),  [TOKEN_IF]          = STR("if"),
    [TOKEN_IN]          = STR("in"),        [TOKEN_LOCAL]       = STR("local"),
    [TOKEN_NIL]         = STR("nil"),       [TOKEN_NOT]         = STR("not"),
    [TOKEN_OR]          = STR("or"),        [TOKEN_REPEAT]      = STR("repeat"),
    [TOKEN_RETURN]      = STR("return"),    [TOKEN_THEN]        = STR("then"),
    [TOKEN_TRUE]        = STR("true"),      [TOKEN_UNTIL]       = STR("until"),
    [TOKEN_WHILE]       = STR("while"),

    [TOKEN_OPEN_PAREN]  = STR("("),         [TOKEN_CLOSE_PAREN] = STR(")"),
    [TOKEN_OPEN_CURLY]  = STR("{"),         [TOKEN_CLOSE_CURLY] = STR("}"),
    [TOKEN_OPEN_BRACE]  = STR("["),         [TOKEN_CLOSE_BRACE] = STR("]"),

    [TOKEN_PLUS]        = STR("+"),         [TOKEN_DASH]        = STR("-"),
    [TOKEN_ASTERISK]    = STR("*"),         [TOKEN_SLASH]       = STR("/"),
    [TOKEN_PERCENT]     = STR("/"),         [TOKEN_CARET]       = STR("^"),

    [TOKEN_EQ]          = STR("=="),        [TOKEN_NOT_EQ]      = STR("~="),
    [TOKEN_LESS]        = STR("<"),         [TOKEN_LESS_EQ]     = STR("<="),
    [TOKEN_GREATER]     = STR(">"),         [TOKEN_GREATER_EQ]  = STR(">="),

    [TOKEN_DOT]         = STR("."),         [TOKEN_CONCAT]      = STR(".."),
    [TOKEN_VARARG]      = STR("..."),       [TOKEN_COMMA]       = STR(","),
    [TOKEN_COLON]       = STR(":"),         [TOKEN_SEMI]        = STR(";"),
    [TOKEN_ASSIGN]      = STR("="),

    [TOKEN_IDENTIFIER]  = STR("<identifier>"),
    [TOKEN_NUMBER]      = STR("<number>"),
    [TOKEN_STRING]      = STR("<string>"),
    [TOKEN_EOF]         = STR("<eof>"),
};

#undef STR

#pragma GCC diagnostic pop
