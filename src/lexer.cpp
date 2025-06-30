#include <stdio.h>
#include <stdlib.h>

#include "lexer.hpp"
#include "vm.hpp"


Lexer
lexer_make(lulu_VM *vm, String source, String script, Builder &b)
{
    const char *ptr = raw_data(script);
    Lexer l{vm, b, source, script, ptr, ptr, 1};
    return l;
}

static bool
is_eof(const Lexer &x)
{
    return x.cursor >= end(x.script);
}

static char
peek(const Lexer &x)
{
    return *x.cursor;
}

static char
peek_next(const Lexer &x)
{
    const char *p = x.cursor + 1;
    // Safe to dereference?
    if (p < end(x.script)) {
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
advance(Lexer &x)
{
    return *x.cursor++;
}

static bool
check(const Lexer &x, char ch)
{
    return peek(x) == ch;
}

static bool
check2(const Lexer &x, char first, char second)
{
    return check(x, first) || check(x, second);
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

static bool
match2(Lexer &x, char first, char second)
{
    return match(x, first) || match(x, second);
}

static String
get_lexeme(const Lexer &x)
{
    return String(x.start, x.cursor);
}

[[noreturn]]
static void
error(const Lexer &x, const char *what)
{
    String where = get_lexeme(x);
    builder_write_string(x.vm, x.builder, where);
    const char *s = builder_to_cstring(x.builder);
    vm_syntax_error(x.vm, x.source, x.line, "%s at '%s'", what, s);
}

static void
expect(Lexer &x, char ch, const char *msg)
{
    if (!match(x, ch)) {
        error(x, msg);
    }
}

/**
 * @note 2025-06-12
 *  Assumptions:
 *  1.) Assumes we just consumed a '[' character.
 */
static int
get_nesting(Lexer &x)
{
    int count = 0;
    while (!is_eof(x) && check(x, '=')) {
        advance(x);
        count++;
    }
    return count;
}

static const char *
skip_multiline(Lexer &x, int nest_open)
{
    for (;;) {
        if (is_eof(x)) {
            error(x, "Unterminated multiline sequence");
        }

        if (match(x, ']')) {
            // `x.cursor` points to the character *after* the ']', so point to
            // the ']' itself so that when we do pointer arithmetic we can get
            // the proper length.
            const char *stop = x.cursor - 1;
            int nest_close = get_nesting(x);
            if (match(x, ']') && nest_open == nest_close) {
                return stop;
            }
            continue;
        }

        char ch = advance(x);
        if (ch == '\n') {
            x.line++;
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
skip_comment(Lexer &x)
{
    // Multiline comment.
    if (match(x, '[')) {
        int nest_open = get_nesting(x);
        if (match(x, '[')) {
            skip_multiline(x, nest_open);
            return;
        }
        // If we didn't find the 2nd '[' then we fall back to single line.
    }
    // Single line
    while (!is_eof(x) && !check(x, '\n')) {
        advance(x);
    }
}

static void
skip_whitespace(Lexer &x)
{
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
            // Skip the two '-'.
            advance(x);
            advance(x);
            skip_comment(x);
            break;
        default:
            return;
        }
    }
}

static char
get_escaped(Lexer &x, char ch)
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

    error(x, "Invalid escape sequence");
}

static Token
make_token(const Lexer &x, Token_Type type, Number n = 0)
{
    Token t{get_lexeme(x), {n}, type, x.line};
    return t;
}

static Token
make_token(const Lexer &x, Token_Type type, String lexeme)
{
    Token t{lexeme, {0.0}, type, x.line};
    return t;
}

static Token
make_token(const Lexer &x, Token_Type type, OString *ostring)
{
    Token t = make_token(x, type);
    t.ostring = ostring;
    return t;
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
is_number(char ch)
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
    return is_alpha(ch) || is_number(ch);
}

static void
consume_sequence(Lexer &x, bool (*predicate)(char ch))
{
    while (!is_eof(x) && predicate(peek(x))) {
        advance(x);
    }
}

static Token
make_number(Lexer &x, char first)
{
    if (first == '0') {
        // Don't consume the (potential) prefix yet.
        char ch = peek(x);
        int base = 0;
        switch (ch) {
        case 'b': base = 2;  break;
        case 'o': base = 8;  break;
        case 'd': base = 10; break;
        case 'x': base = 16; break;
        default:
            // If it's a number or a newline, do nothing.
            if (is_alpha(ch)) {
                advance(x);
                error(x, "Invalid integer prefix");
            }
            break;
        }

        // Consume everything, don't check if it's a bad number yet.
        if (base != 0) {
            consume_sequence(x, is_ident);
            String s = get_lexeme(x);
            char  *last;
            // Skip the `0[bodx]` prefix because `strto*` doesn't support `0b`.
            unsigned long ul = strtoul(raw_data(s) + 2, &last, base);
            if (last != end(s)) {
                char buf[32];
                sprintf(buf, "Invalid base-%i integer", base);
                error(x, buf);
            }
            return make_token(x, TOKEN_NUMBER, Number(ul));
        }
        // TODO(2025-06-12): Accept leading zeroes? Lua does, Python doesn't
    }

    // Consume '1.2.3'
    do {
        consume_sequence(x, is_number);
    } while (match(x, '.'));

    // Exponent form?
    if (match2(x, 'e', 'E')) {
        match2(x, '+', '-'); // optional sign
        consume_sequence(x, is_number);
    }
    consume_sequence(x, is_ident);

    String s = get_lexeme(x);
    char *last;
    Number d = strtod(raw_data(s), &last);
    if (last != end(s)) {
        error(x, "Malformed number");
    }
    return make_token(x, TOKEN_NUMBER, d);
}

static Token
make_string(Lexer &x, char q)
{
    lulu_VM *vm = x.vm;
    Builder &b  = vm_get_builder(vm);
    String   s  = String(x.cursor, x.cursor);
    while (!is_eof(x) && !check2(x, q, '\n')) {
        char ch = advance(x);
        if (ch == '\\') {
            // 'flush' the string up to this point.
            builder_write_string(vm, b, s);

            // Read the character after '\'.
            ch = advance(x);
            ch = get_escaped(x, ch);
            builder_write_char(vm, b, ch);

            // Point to after the escape character.
            s = String(x.cursor, x.cursor);
        } else {
            s.len += 1;
        }
    }
    expect(x, q, "Unterminated string");
    builder_write_string(vm, b, s);
    s = builder_to_string(b);
    OString *o = ostring_new(vm, s);
    return make_token(x, TOKEN_STRING, o);
}

static Token
check_keyword(const Lexer &x, String s, Token_Type type)
{
    if (s == token_strings[type]) {
        return make_token(x, type, s);
    }
    return make_token(x, TOKEN_IDENTIFIER, s);
}

static Token
make_keyword_or_identifier(const Lexer &x)
{
    String word = get_lexeme(x);

    // If we reached this point then `word` MUST be at least of length 1.
    switch (word[0]) {
    case 'a': return check_keyword(x, word, TOKEN_AND);
    case 'b': return check_keyword(x, word, TOKEN_BREAK);
    case 'd': return check_keyword(x, word, TOKEN_DO);
    case 'e':
        switch (len(word)) {
        case 3: return check_keyword(x, word, TOKEN_END);
        case 4: return check_keyword(x, word, TOKEN_ELSE);
        case 6: return check_keyword(x, word, TOKEN_ELSEIF);
        }
        break;
    case 'f':
        switch (len(word)) {
        case 3: return check_keyword(x, word, TOKEN_FOR);
        case 5: return check_keyword(x, word, TOKEN_FALSE);
        case 8: return check_keyword(x, word, TOKEN_FUNCTION);
        }
        break;
    case 'i':
        if (len(word) != 2) {
            break;
        }
        switch (word[1]) {
        case 'f': return check_keyword(x, word, TOKEN_IF);
        case 'n': return check_keyword(x, word, TOKEN_IN);
        }
        break;
    case 'l': return check_keyword(x, word, TOKEN_LOCAL);
    case 'n':
        if (len(word) != 3) {
            break;
        }
        switch (word[1]) {
        case 'i': return check_keyword(x, word, TOKEN_NIL);
        case 'o': return check_keyword(x, word, TOKEN_NOT);
        }
        break;
    case 'o': return check_keyword(x, word, TOKEN_OR);
    case 'r':
        if (len(word) != 6) {
            break;
        }
        // 'repeat' and 'return' have the same first 2 characters
        switch (word[2]) {
        case 't': return check_keyword(x, word, TOKEN_RETURN);
        case 'p': return check_keyword(x, word, TOKEN_REPEAT);
        }
        break;
    case 't':
        if (len(word) != 4) {
            break;
        }
        switch (word[1]) {
        case 'h': return check_keyword(x, word, TOKEN_THEN);
        case 'r': return check_keyword(x, word, TOKEN_TRUE);
        }
        break;
    case 'u': return check_keyword(x, word, TOKEN_UNTIL);
    case 'w': return check_keyword(x, word, TOKEN_WHILE);
    default:
        break;
    }

    return make_token(x, TOKEN_IDENTIFIER);
}

Token
lexer_lex(Lexer &x)
{
    skip_whitespace(x);
    x.start = x.cursor;
    if (is_eof(x)) {
        return make_token(x, TOKEN_EOF);
    }

    char ch = advance(x);
    if (is_alpha(ch)) {
        consume_sequence(x, is_ident);
        return make_keyword_or_identifier(x);
    } else if (is_number(ch)) {
        return make_number(x, ch);
    }

    Token_Type type = TOKEN_INVALID;
    switch (ch) {
    case '(': type = TOKEN_OPEN_PAREN; break;
    case ')': type = TOKEN_CLOSE_PAREN; break;
    case '{': type = TOKEN_OPEN_CURLY; break;
    case '}': type = TOKEN_CLOSE_CURLY; break;
    case '[':
        // Don't consume '[' nor '=' yet; need to get rid of all '=' first.
        if (check2(x, '[', '=')) {
            int nest_open = get_nesting(x);
            expect(x, '[', "Expected 2nd '[' to start off multiline string");
            const char *start = x.cursor;
            const char *stop  = skip_multiline(x, nest_open);
            return make_token(x, TOKEN_STRING, String(start, stop));
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
        expect(x, '=', "Expected '='");
        type = TOKEN_NOT_EQ;
        break;
    case '=': type = match(x, '=') ? TOKEN_EQ : TOKEN_ASSIGN; break;
    case '<': type = match(x, '=') ? TOKEN_LESS_EQ : TOKEN_LESS; break;
    case '>': type = match(x, '=') ? TOKEN_GREATER : TOKEN_GREATER_EQ; break;

    case '.':
        if (match(x, '.')) {
            type = match(x, '.') ? TOKEN_VARARG : TOKEN_CONCAT;
        } else {
            if (is_number(peek(x))) {
                return make_number(x, ch);
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
    return make_token(x, type);
}

/**
 * @note 2025-06-14:
 *  -   ORDER: Keep in sync with `Token_Type`!
 */
const String token_strings[TOKEN_COUNT] = {
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
    "."_s, ".."_s, "..."_s, ","_s, ":"_s, ";"_s, "="_s,
    "<identifier>"_s, "<number>"_s, "<string>"_s, "<eof>"_s,
};
