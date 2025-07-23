#include <stdio.h>
#include <stdlib.h>

#include "lexer.hpp"
#include "vm.hpp"


Lexer
lexer_make(lulu_VM *vm, OString *source, const LString &script, Builder *b)
{
    Lexer x;
    x.vm      = vm;
    x.builder = b;
    x.source  = source;
    x.script  = script;
    x.cursor  = raw_data(script);
    x.start   = raw_data(script);
    x.line    = 1;
    return x;
}

static bool
_is_eof(const Lexer *x)
{
    return x->cursor >= end(x->script);
}

static char
_peek(const Lexer *x)
{
    return *x->cursor;
}

static char
_peek_next(const Lexer *x)
{
    const char *p = x->cursor + 1;
    // Safe to dereference?
    if (p < end(x->script)) {
        return *p;
    }
    return '\0';
}

/**
 * @brief
 *  -   Increments the cursor the returns the character we were pointing to
 *      just before the increment.
 */
static char
_advance(Lexer *x)
{
    return *x->cursor++;
}

static bool
_check(const Lexer *x, char ch)
{
    return _peek(x) == ch;
}

static bool
_check2(const Lexer *x, char first, char second)
{
    return _check(x, first) || _check(x, second);
}

static bool
_match(Lexer *x, char ch)
{
    bool found = _check(x, ch);
    if (found) {
        _advance(x);
    }
    return found;
}

static bool
_match2(Lexer *x, char first, char second)
{
    return _match(x, first) || _match(x, second);
}

static LString
get_lexeme(const Lexer *x)
{
    return slice_pointer(x->start, x->cursor);
}

[[noreturn]]
static void
_error(const Lexer *x, const char *what)
{
    LString where = get_lexeme(x);
    builder_write_lstring(x->vm, x->builder, where);
    const char *s = builder_to_cstring(*x->builder);
    vm_syntax_error(x->vm, x->source, x->line, "%s at '%s'", what, s);
}

static void
_expect(Lexer *x, char ch, const char *msg)
{
    if (!_match(x, ch)) {
        _error(x, msg);
    }
}

/**
 * @note 2025-06-12
 *  Assumptions:
 *  1.) Assumes we just consumed a '[' character.
 */
static int
_get_nesting(Lexer *x)
{
    int count = 0;
    while (!_is_eof(x) && _check(x, '=')) {
        _advance(x);
        count++;
    }
    return count;
}

static const char *
_skip_multiline(Lexer *x, int nest_open)
{
    for (;;) {
        if (_is_eof(x)) {
            _error(x, "Unterminated multiline sequence");
        }

        if (_match(x, ']')) {
            // `x->cursor` points to the character *after* the ']', so point to
            // the ']' itself so that when we do pointer arithmetic we can get
            // the proper length.
            const char *stop = x->cursor - 1;
            int nest_close = _get_nesting(x);
            if (_match(x, ']') && nest_open == nest_close) {
                return stop;
            }
            continue;
        }

        char ch = _advance(x);
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
_skip_comment(Lexer *x)
{
    // Multiline comment.
    if (_match(x, '[')) {
        int nest_open = _get_nesting(x);
        if (_match(x, '[')) {
            _skip_multiline(x, nest_open);
            return;
        }
        // If we didn't find the 2nd '[' then we fall back to single line.
    }
    // Single line
    while (!_is_eof(x) && !_check(x, '\n')) {
        _advance(x);
    }
}

static void
_skip_whitespace(Lexer *x)
{
    for (;;) {
        char ch = _peek(x);
        switch (ch) {
        case '\n': x->line++; // fall-through
        case ' ':
        case '\r':
        case '\t':
            _advance(x);
            break;
        case '-':
            if (_peek_next(x) != '-') {
                return;
            }
            // Skip the two '-'.
            _advance(x);
            _advance(x);
            _skip_comment(x);
            break;
        default:
            return;
        }
    }
}

static char
_get_escaped(Lexer *x, char ch)
{
    switch (ch) {
    case '0':   return '\0';
    case 'a':   return '\a';
    case 'b':   return '\b';
    case 'f':   return '\f';
    case 'n':   return '\n';
    case 't':   return '\t';
    case 'r':   return '\r';
    case '\'':  // fall-through
    case '\"':  // fall-through
    case '\\':  return ch;
    default:
        break;
    }

    _error(x, "Invalid escape sequence");
}

static Token
_make_token(const Lexer *x, Token_Type type)
{
    Token t = Token::make(type, x->line, get_lexeme(x), 0);
    return t;
}

static Token
_make_token_number(const Lexer *x, Number n)
{
    Token t = _make_token(x, TOKEN_NUMBER);
    t.number = n;
    return t;
}

static Token
_make_token_lexeme(const Lexer *x, Token_Type type, const LString &lexeme)
{
    Token t = Token::make(type, x->line, lexeme, 0);
    return t;
}

static Token
_make_token_ostring(const Lexer *x, OString *ostring)
{
    Token t = _make_token(x, TOKEN_STRING);
    t.ostring = ostring;
    return t;
}

static bool
_is_upper(char ch)
{
    return 'A' <= ch && ch <= 'Z';
}

static bool
_is_lower(char ch)
{
    return 'a' <= ch && ch <= 'z';
}

static bool
_is_number(char ch)
{
    return '0' <= ch && ch <= '9';
}

static bool
_is_alpha(char ch)
{
    return _is_upper(ch) || _is_lower(ch) || ch == '_';
}

static bool
_is_ident(char ch)
{
    return _is_alpha(ch) || _is_number(ch);
}

static void
_consume_sequence(Lexer *x, bool (*predicate)(char ch))
{
    while (!_is_eof(x) && predicate(_peek(x))) {
        _advance(x);
    }
}

static Token
_make_number(Lexer *x, char first)
{
    if (first == '0') {
        // Don't consume the (potential) prefix yet.
        char ch = _peek(x);
        int base = 0;
        switch (ch) {
        case 'b': base = 2;  break;
        case 'o': base = 8;  break;
        case 'd': base = 10; break;
        case 'x': base = 16; break;
        default:
            // If it's a number or a newline, do nothing.
            if (_is_alpha(ch)) {
                _advance(x);
                _error(x, "Invalid integer prefix");
            }
            break;
        }

        // Consume everything, don't check if it's a bad number yet.
        if (base != 0) {
            _consume_sequence(x, _is_ident);
            LString s = get_lexeme(x);
            Number d;
            if (!lstring_to_number(s, &d, base)) {
                char buf[32];
                sprintf(buf, "Invalid base-%i integer", base);
                _error(x, buf);
            }
            return _make_token_number(x, d);
        }
        // TODO(2025-06-12): Accept leading zeroes? Lua does, Python doesn't
    }

    // Consume '1.2.3'
    do {
        _consume_sequence(x, _is_number);
    } while (_match(x, '.'));

    // Exponent form?
    if (_match2(x, 'e', 'E')) {
        _match2(x, '+', '-'); // optional sign
        _consume_sequence(x, _is_number);
    }
    _consume_sequence(x, _is_ident);

    LString s = get_lexeme(x);
    Number d;
    if (!lstring_to_number(s, &d)) {
        _error(x, "Malformed number");
    }
    return _make_token_number(x, d);
}

static Token
_make_string(Lexer *x, char q)
{
    lulu_VM *vm = x->vm;
    Builder *b  = vm_get_builder(vm);
    LString  s{x->cursor, 0};
    while (!_is_eof(x) && !_check2(x, q, '\n')) {
        char ch = _advance(x);
        if (ch == '\\') {
            // 'flush' the string up to this point.
            builder_write_lstring(vm, b, s);

            // Read the character after '\'.
            ch = _advance(x);
            ch = _get_escaped(x, ch);
            builder_write_char(vm, b, ch);

            // Point to after the escape character.
            s = {x->cursor, 0};
        } else {
            s.len += 1;
        }
    }
    _expect(x, q, "Unterminated string");
    builder_write_lstring(vm, b, s);
    s = builder_to_string(*b);
    OString *o = ostring_new(vm, s);
    return _make_token_ostring(x, o);
}

static Token
_check_keyword(const Lexer *x, const LString &s, Token_Type type)
{
    if (slice_eq(s, token_strings[type])) {
        return _make_token_lexeme(x, type, s);
    }
    return _make_token_lexeme(x, TOKEN_IDENTIFIER, s);
}

static Token
_make_keyword_or_identifier(const Lexer *x)
{
    LString word = get_lexeme(x);

    // If we reached this point then `word` MUST be at least of length 1.
    switch (word[0]) {
    case 'a': return _check_keyword(x, word, TOKEN_AND);
    case 'b': return _check_keyword(x, word, TOKEN_BREAK);
    case 'd': return _check_keyword(x, word, TOKEN_DO);
    case 'e':
        switch (len(word)) {
        case 3: return _check_keyword(x, word, TOKEN_END);
        case 4: return _check_keyword(x, word, TOKEN_ELSE);
        case 6: return _check_keyword(x, word, TOKEN_ELSEIF);
        }
        break;
    case 'f':
        switch (len(word)) {
        case 3: return _check_keyword(x, word, TOKEN_FOR);
        case 5: return _check_keyword(x, word, TOKEN_FALSE);
        case 8: return _check_keyword(x, word, TOKEN_FUNCTION);
        }
        break;
    case 'i':
        if (len(word) != 2) {
            break;
        }
        switch (word[1]) {
        case 'f': return _check_keyword(x, word, TOKEN_IF);
        case 'n': return _check_keyword(x, word, TOKEN_IN);
        }
        break;
    case 'l': return _check_keyword(x, word, TOKEN_LOCAL);
    case 'n':
        if (len(word) != 3) {
            break;
        }
        switch (word[1]) {
        case 'i': return _check_keyword(x, word, TOKEN_NIL);
        case 'o': return _check_keyword(x, word, TOKEN_NOT);
        }
        break;
    case 'o': return _check_keyword(x, word, TOKEN_OR);
    case 'r':
        if (len(word) != 6) {
            break;
        }
        // 'repeat' and 'return' have the same first 2 characters
        switch (word[2]) {
        case 't': return _check_keyword(x, word, TOKEN_RETURN);
        case 'p': return _check_keyword(x, word, TOKEN_REPEAT);
        }
        break;
    case 't':
        if (len(word) != 4) {
            break;
        }
        switch (word[1]) {
        case 'h': return _check_keyword(x, word, TOKEN_THEN);
        case 'r': return _check_keyword(x, word, TOKEN_TRUE);
        }
        break;
    case 'u': return _check_keyword(x, word, TOKEN_UNTIL);
    case 'w': return _check_keyword(x, word, TOKEN_WHILE);
    default:
        break;
    }

    return _make_token(x, TOKEN_IDENTIFIER);
}

Token
lexer_lex(Lexer *x)
{
    _skip_whitespace(x);
    x->start = x->cursor;
    if (_is_eof(x)) {
        return _make_token(x, TOKEN_EOF);
    }

    char ch = _advance(x);
    if (_is_alpha(ch)) {
        _consume_sequence(x, _is_ident);
        return _make_keyword_or_identifier(x);
    } else if (_is_number(ch)) {
        return _make_number(x, ch);
    }

    Token_Type type = TOKEN_INVALID;
    switch (ch) {
    case '(': type = TOKEN_OPEN_PAREN; break;
    case ')': type = TOKEN_CLOSE_PAREN; break;
    case '{': type = TOKEN_OPEN_CURLY; break;
    case '}': type = TOKEN_CLOSE_CURLY; break;
    case '[':
        // Don't consume '[' nor '=' yet; need to get rid of all '=' first.
        if (_check2(x, '[', '=')) {
            int nest_open = _get_nesting(x);
            _expect(x, '[', "Expected 2nd '[' to start off multiline string");
            const char *start = x->cursor;
            const char *stop  = _skip_multiline(x, nest_open);
            return _make_token_lexeme(x, TOKEN_STRING, slice_pointer(start, stop));
        }
        type = TOKEN_OPEN_BRACE;
        break;
    case ']': type = TOKEN_CLOSE_BRACE; break;

    case '+': type = TOKEN_PLUS; break;
    case '-': type = TOKEN_DASH; break;
    case '*': type = TOKEN_ASTERISK; break;
    case '/': type = TOKEN_SLASH; break;
    case '%': type = TOKEN_PERCENT; break;
    case '^': type = TOKEN_CARET; break;

    case '~':
        _expect(x, '=', "Expected '='");
        type = TOKEN_NOT_EQ;
        break;
    case '=': type = _match(x, '=') ? TOKEN_EQ : TOKEN_ASSIGN; break;
    case '<': type = _match(x, '=') ? TOKEN_LESS_EQ : TOKEN_LESS; break;
    case '>': type = _match(x, '=') ? TOKEN_GREATER : TOKEN_GREATER_EQ; break;

    case '#': type = TOKEN_POUND; break;
    case '.':
        if (_match(x, '.')) {
            type = _match(x, '.') ? TOKEN_VARARG : TOKEN_CONCAT;
        } else {
            if (_is_number(_peek(x))) {
                return _make_number(x, ch);
            }
            type = TOKEN_DOT;
        }
        break;
    case ',': type = TOKEN_COMMA; break;
    case ';': type = TOKEN_SEMI; break;

    case '\'':
    case '\"': return _make_string(x, ch);
    }
    if (type == TOKEN_INVALID) {
        _error(x, "Unexpected character");
    }
    return _make_token(x, type);
}

/**
 * @note 2025-06-14:
 *  -   ORDER: Keep in sync with `Token_Type`!
 */
const LString token_strings[TOKEN_COUNT] = {
    "<invalid>"_s,

    // Keywords
    "and"_s, "break"_s, "do"_s, "else"_s, "elseif"_s, "end"_s,
    "false"_s, "for"_s, "function"_s, "if"_s, "in"_s,
    "local"_s, "nil"_s, "not"_s, "or"_s, "repeat"_s,
    "return"_s, "then"_s, "true"_s, "until"_s, "while"_s,

    // Balanced pairs
    "("_s, ")"_s, "{"_s, "}"_s, "["_s, "]"_s,

    // Arithmetic Operators
    "+"_s, "-"_s, "*"_s, "/"_s, "/"_s, "^"_s,

    // Relational Operators
    "=="_s, "~="_s, "<"_s, "<="_s, ">"_s, ">="_s,

    // Misc.
    "#"_s, "."_s, ".."_s, "..."_s, ","_s, ":"_s, ";"_s, "="_s,
    "<identifier>"_s, "<number>"_s, "<string>"_s, "<eof>"_s,
};
