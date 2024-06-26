#include <array>
#include <string_view>
#include <cctype> // isalpha, isalnum, isdigit, isxdigit, etc.
#include "lexer.hpp"

using Type = Token::Type;

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
static char next_char(Lexer *ls)
{
    ls->current = getc_stream(ls->stream);
    return ls->current;
}

// Advances string view AND appends it to the buffer.
// llex.c:save_and_next(ls)
static char consume_char(Lexer *ls)
{
    save_char(ls, ls->current);
    return next_char(ls);
}

static bool match_char(Lexer *ls, char ch)
{
    if (ls->current == ch) {
        // consume_char(ls);
        return true;
    } else {
        return false;
    }
}

void init_lexer(Lexer *ls, Global *g, Stream *z, Buffer *b)
{
    ls->global = g;
    ls->stream = z;
    ls->buffer = b;
    ls->line   = 1;
    resize_buffer(ls->global, ls->buffer, MIN_BUFFER);
    next_char(ls); // Read first char
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
        // Do NOT call peek_char here, we might end up with a bad string view.
        switch (ls->current) {
        case '\n': ls->line++; // fall through
        case '\r':
        case '\t':
        case ' ': 
            next_char(ls);
            break;
        default:
            return;
        }
    }
}

static Token consume_token(Lexer *ls, Token::Type t)
{
    consume_char(ls);
    return make_token(ls, t);
}

Token scan_token(Lexer *ls)
{
    reset_buffer(ls->buffer);
    skip_whitespace(ls);
    
    char ch = ls->current;
    if (ch == '\0')
        return make_token(ls, Type::Eof);
    
    if (isalnum(ch) || ch == '_') {
        do {
            consume_char(ls);
        } while (isalnum(ls->current) || ls->current == '_');
        return make_token(ls, Type::Identifier);
    }

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
    case '<': return consume_token(ls, Type::LAngle);
    case '>': return consume_token(ls, Type::RAngle);
    case '=': return consume_token(ls, Type::Equal1);
              
    // Punctuation
    case ',': return consume_token(ls, Type::Comma);
    case '.': return consume_token(ls, Type::Dot1);
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
        // Skip the quote.
        next_char(ls);
        while (!match_char(ls, ch) && !match_char(ls, '\0')) {
            consume_char(ls);
        }
        // Is closed off properly?
        if (match_char(ls, ch)) {
            next_char(ls);
            return make_token(ls, Type::String);
        }
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
