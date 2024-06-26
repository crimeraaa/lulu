#include <cctype> // isalpha, isalnum, isdigit, isxdigit, etc.
#include "lexer.hpp"

using Type = Token::Type;

static char peek_char(Lexer *ls)
{
    return peek_stream(ls->stream);
}

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

static char next_char(Lexer *ls)
{
    ls->current = getc_stream(ls->stream);
    return ls->current;
}

static char save_and_next_char(Lexer *ls)
{
    save_char(ls, ls->current);
    return next_char(ls);
}

static void init_slice(Slice *s, const char *cs = nullptr)
{
    s->string = cs;
    s->length = (cs != nullptr) ? std::strlen(cs) : 0;
}

void init_lexer(Lexer *ls, Global *g, Stream *z, Buffer *b)
{
    init_slice(&ls->lexeme);
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
        // Do NOT call peek_char here.
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

Token scan_token(Lexer *ls)
{
    reset_buffer(ls->buffer);
    skip_whitespace(ls);
    
    char ch = peek_char(ls);
    if (ch == '\0')
        return make_token(ls, Type::Eof);

    if (isalnum(ls->current) || ls->current == '_') {
        do {
            save_and_next_char(ls);
        } while (isalnum(ls->current) || ls->current == '_');
        return make_token(ls, Type::Identifier);
    }
    // Single char tokens
    save_and_next_char(ls);
    return error_token(ls);
}
