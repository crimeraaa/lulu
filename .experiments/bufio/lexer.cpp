#include <array>
#include <string_view>
#include <cctype> // isalpha, isalnum, isdigit, isxdigit, etc.
#include "lexer.hpp"

using Type = Token::Type;

static bool is_eof(Lexer *ls)
{
    return ls->current == '\0';
}

static bool check_char(Lexer *ls, char ch)
{
    return ls->current == ch;
}

static char peek_current(Lexer *ls)
{
    return ls->current;
}

// static char peek_next(Lexer *ls)
// {
//     if (is_eof(ls))
//         return '\0';
//     // Similar to instruction pointer, this points to 1 past the current.
//     return ls->stream->position[0];
// }

// https://www.lua.org/source/5.1/llex.c.html#save
static void save_char(Lexer *ls, char ch)
{
    Buffer *b = ls->buffer;
    // Need to resize?
    if (b->length + 1 > b->capacity) {
        if (b->capacity >= SIZE_MAX / 2)
            throw std::bad_alloc();
        resize_buffer(ls->global, b, b->capacity * 2);
    }    
    b->buffer[b->length++] = ch;
}

// Advances our string view, but does not save it to the buffer.
// llex.c:next
static char skip_char(Lexer *ls)
{
    ls->current = getc_stream(ls->stream);
    return ls->current;
}

// Advance our string view if it matches, but do not modify the buffer at all.
static bool skip_char_if(Lexer *ls, char ch)
{
    if (check_char(ls, ch)) {
        skip_char(ls);
        return true;
    }
    return false;
}

// Advances string view AND appends it to the buffer. Returns newly read char.
// llex.c:save_and_next()
static char consume_char(Lexer *ls)
{
    save_char(ls, ls->current);
    return skip_char(ls);
}

// If matches, we consume the character. This affects the stream and buffer.
static bool match_char(Lexer *ls, char ch)
{
    if (check_char(ls, ch)) {
        consume_char(ls);
        return true;
    }
    return false;
}

void init_lexer(Lexer *ls, Global *g, Stream *z, Buffer *b)
{
    ls->global = g;
    ls->stream = z;
    ls->buffer = b;
    ls->line   = 1;
    resize_buffer(ls->global, ls->buffer, MIN_BUFFER);
    skip_char(ls); // Read first char
}

static Token make_token(Lexer *ls, Type type)
{
    Buffer *b = ls->buffer;
    Token   t;
    t.type = type;    
    t.data = copy_string(ls->global, b->buffer, b->length);
    t.line = ls->line;
    return t;
}

static Token error_token(Lexer *ls)
{
    return make_token(ls, Type::Error);
}

static void skip_whitespace(Lexer *ls)
{
    for (;;) {
        switch (peek_current(ls)) {
        case '\n': ls->line++; // fall through
        case '\r':
        case '\t':
        case ' ': 
            skip_char(ls);
            break;
        default:
            return;
        }
    }
}

// Creates a token including the currently viewing character.
static Token consume_token(Lexer *ls, Token::Type t)
{
    consume_char(ls);
    return make_token(ls, t);
}

static Token string_token(Lexer *ls, char q)
{
    // Skip opening quote.
    skip_char(ls);
    while (!check_char(ls, q)) {
        if (check_char(ls, '\n') || is_eof(ls))
            goto Error;
        consume_char(ls);
    }
    // Is closed off properly? Skip (not consume!) the closing quote.
    if (skip_char_if(ls, q))
        return make_token(ls, Type::String);
Error:
    return error_token(ls);
}

static void decimal_token(Lexer *ls)
{
Decimal:
    do {
        consume_char(ls);
    } while (isdigit(peek_current(ls)));
    
    // Have an exponent?
    if (match_char(ls, 'e')) {
        char ch = peek_current(ls);
        // Have explicit signedness?
        if (ch == '+' || ch == '-')
            consume_char(ls);
        goto Decimal;
    }
    
    // Have a decimal point?
    if (match_char(ls, '.'))
        goto Decimal;
}

static Token number_token(Lexer *ls)
{
    decimal_token(ls);
    return make_token(ls, Type::Number);
}

static Token identifier_token(Lexer *ls)
{
    // Consume first letter until we hit a non-identifier.
    do {
        consume_char(ls);
    } while (isalnum(peek_current(ls)) || peek_current(ls) == '_');
    return make_token(ls, Type::Identifier);
}

static Token make_token_if(Lexer *ls, char ch, Token::Type y, Token::Type n)
{
    return make_token(ls, match_char(ls, ch) ? y : n);
}

static Token equals_token(Lexer *ls, Token::Type y, Token::Type n = Type::Error)
{
    // Consume the angle bracket or first equals sign.
    consume_char(ls);
    return make_token_if(ls, '=', y, n);
}

static Token dots_token(Lexer *ls)
{
    // Consume and save the first dot.
    consume_char(ls);

    // We have another dot?
    if (match_char(ls, '.'))
        return make_token_if(ls, '.', Type::Dot3, Type::Dot2);
    else
        return make_token(ls, Type::Dot1);
}

Token scan_token(Lexer *ls)
{
    reset_buffer(ls->buffer);
    skip_whitespace(ls);
    
    if (is_eof(ls))
        return make_token(ls, Type::Eof);
    
    // Save now as ls->current will likely get updated as we move along.
    char ch = peek_current(ls);
    if (isdigit(ch))
        return number_token(ls);
    if (isalpha(ch) || ch == '_')
        return identifier_token(ls);

    // Single char tokens
    switch (ch) {
    // Brackets
    case '(': return consume_token(ls, Type::LParen);
    case ')': return consume_token(ls, Type::RParen);
    case '[': return consume_token(ls, Type::LSquare);
    case ']': return consume_token(ls, Type::RSquare);
    case '{': return consume_token(ls, Type::LCurly);
    case '}': return consume_token(ls, Type::RCurly);
    
    // Relational Operators
    // case '<': return consume_token(ls, Type::LAngle);
    case '<': return equals_token(ls, Type::LAngleEq, Type::LAngle);
    case '>': return equals_token(ls, Type::RAngleEq, Type::RAngle);
    case '=': return equals_token(ls, Type::Equal2,   Type::Equal1);
    case '~': return equals_token(ls, Type::TildeEq);
              
    // Punctuation
    case ',': return consume_token(ls, Type::Comma);
    case '.': return dots_token(ls);
    case ':': return consume_token(ls, Type::Colon);
    case ';': return consume_token(ls, Type::Semicolon);

    // Arithmetic Operators
    case '+': return consume_token(ls, Type::Plus);
    case '-': return consume_token(ls, Type::Dash);
    case '*': return consume_token(ls, Type::Star);
    case '/': return consume_token(ls, Type::Slash);
    case '%': return consume_token(ls, Type::Percent);
    case '^': return consume_token(ls, Type::Caret);
              
    case '\'':
    case '\"':
        return string_token(ls, ch);
    }
    return error_token(ls);
}

template<class K, class V, size_t N>
class LookupTable {
public:
    constexpr V& operator[](K key)
    {
        return m_table[cast<size_t>(key)];
    }
    constexpr const V& operator[](K key) const
    {
        return m_table[cast<size_t>(key)];
    }
private:
    std::array<V, N> m_table;
};

#define T Token::Type

// Constexpr lambdas are a C++17 thing.
static constexpr auto TOKEN_TO_STRING = []() constexpr
{
    using K = Token::Type;
    using V = struct {
        std::string_view name;
        std::string_view display;
    };
    LookupTable<K, V, TOKEN_COUNT> t;
    // Brackets
    t[T::LParen]   = {"LParen",   "("},  t[T::RParen]  = {"RParen",   ")"},
    t[T::LSquare]  = {"LSquare",  "["},  t[T::RSquare] = {"RSquare",  "]"},
    t[T::LCurly]   = {"LCurly",   "{"},  t[T::RCurly]  = {"RCurly",   "}"},

    // Relational Operators
    t[T::LAngle]   = {"LAngle",   "<"},  t[T::RAngle]   = {"RAngle",   ">"},
    t[T::LAngleEq] = {"LAngleEq", "<="}, t[T::RAngleEq] = {"RAngleEq", ">="},
    t[T::Equal1]   = {"Equal1",   "="},  t[T::Equal2]   = {"Equal2",   "=="},
    t[T::TildeEq]  = {"TildeEq",  "~"},

    // Punctuation
    t[T::Dot1]     = {"Dot1", "."},   t[T::Dot2] = {"Dot2", ".."}, 
    t[T::Dot3]     = {"Dot3", "..."}, t[T::Comma] = {"Comma", ","},
    t[T::Colon]    = {"Colon", ":"},  t[T::Semicolon] = {"Semicolon", ";"},

    // Arithmetic Operators
    t[T::Plus]     = {"Plus", "+"},     t[T::Dash]  = {"Dash", "-"},
    t[T::Star]     = {"Star", "*"},     t[T::Slash] = {"Slash", "/"},
    t[T::Percent]  = {"Percent", "%"},  t[T::Caret] = {"Caret", "/"},

    // Other
    t[T::Identifier] = {"Identifier", "<identifier>"},
    t[T::String]     = {"String", "<string>"},
    t[T::Number]     = {"Number", "<number>"},
    t[T::Error]      = {"Error", "<error>"},
    t[T::Eof]        = {"Eof", "<eof>"};
    return t;
}();

#undef T

const char *name_token(Token::Type t)
{
    return TOKEN_TO_STRING[t].name.data();
}

const char *display_token(Token::Type t)
{
    return TOKEN_TO_STRING[t].display.data();
}
