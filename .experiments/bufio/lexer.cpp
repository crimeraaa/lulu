#include <array>
#include <string_view>
#include <cctype> // isalpha, isalnum, isdigit, isxdigit, etc.
#include "lexer.hpp"

using Type = Token::Type;

static std::string_view token_to_string(Token::Type t);

static bool is_eof(Lexer *ls)
{
    return ls->current == '\0';
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

// Advances our string view, but does not save it to the buffer.
// Returns the newly read character.
// llex.c:next
static char skip_char(Lexer *ls)
{
    ls->current = read_stream(ls->stream);
    return ls->current;
}

static bool skip_char_if(Lexer *ls, char ch)
{
    bool found = ls->current == ch;
    if (found)
        skip_char(ls);
    return found;
}

// Returns the character right after ls->current without changing any state.
static char lookahead_char(Lexer *ls)
{
    return lookahead_stream(ls->stream);
}

// Saves `ls->current` to buffer then advances our string view.
// llex.c:save_and_next()
static char consume_char(Lexer *ls)
{
    save_char(ls, ls->current);
    return skip_char(ls);
}

// If matches, we consume the character. This affects the stream and buffer.
static bool match_char(Lexer *ls, char ch)
{
    if (ls->current == ch) {
        consume_char(ls);
        return true;
    }
    return false;
}

static bool check_char_in(Lexer *ls, const char *set)
{
    // Get pointer to first occurence of `ls->currrent` in `set`.
    // If not found we get `NULL`.
    return std::strchr(set, ls->current) != nullptr;
}

static bool match_char_in(Lexer *ls, const char *set)
{
    bool found = check_char_in(ls, set);
    if (found)
        consume_char(ls);
    return found;
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
    Token t;
    t.type = type;
    t.line = ls->line;
    return t;
}

static String *buffer_to_string(Lexer *ls, int bufpad, int lenpad)
{
    Global *g = ls->global; 
    Buffer *b = ls->buffer;
    return copy_string(g, view_buffer(b, bufpad), length_buffer(b) + lenpad);
}

// Overload for no padding whatsoever.
static String *buffer_to_string(Lexer *ls)
{
    return buffer_to_string(ls, 0, 0);
}

static Token error_token(Lexer *ls)
{
    Token   t = make_token(ls, Type::Error);
    String *s = buffer_to_string(ls); // TODO: Remove newlines beforehand
    t.data.string = s;
    return t;
}

static int get_nesting(Lexer *ls)
{
    int nest = 0;
    while (skip_char_if(ls, '=')) {
        nest += 1;
    }
    return nest;
}

static void skip_long_comment(Lexer *ls, int left)
{
    for (;;) {
        if (skip_char_if(ls, ']')) {
            int right = get_nesting(ls);
            if (left == right && skip_char_if(ls, ']'))
                return;
        }
        if (is_eof(ls))
            break;
        if (check_char_in(ls, "\r\n"))
            ls->line += 1;
        skip_char(ls);
    }
}

static void skip_simple_comment(Lexer *ls)
{
    // Skip a single line comment and don't consume the newline.
    while (!check_char_in(ls, "\r\n")) {
        skip_char(ls);
    }
}

// Assumes we already skipped the 2 opening dashes.
static void skip_comment(Lexer *ls)
{
    // Might have multiline comment?
    if (skip_char_if(ls, '[')) {
        int nest = get_nesting(ls);
        // Don't have 2 left square brackets?
        if (!skip_char_if(ls, '['))
            skip_simple_comment(ls);
        else
            skip_long_comment(ls, nest);
    } else {
        skip_simple_comment(ls);
    }
}

static void skip_whitespace(Lexer *ls)
{
    for (;;) {
        switch (ls->current) {
        case '\n': ls->line++; // fall through
        case '\r':
        case '\t':
        case ' ': 
            skip_char(ls);
            break;
        case '-': 
            // Don't consume just yet.
            if (lookahead_char(ls) != '-')
                return;
            // Skip the 2 '-' tokens.
            skip_char(ls);
            skip_char(ls);
            skip_comment(ls);
            break;
        default:
            return;
        }
    }
}

static Token string_token(Lexer *ls, char q)
{
    // Consume opening quote, then its contents, until we reach closing quote.
    do {
        // Don't consume the escape character.
        if (ls->current == '\\') {
            char ch;
            // Skip the slash, look at the character right after it.
            switch (skip_char(ls)) {
            case '0': ch = '\0'; break;
            case 'a': ch = '\a'; break;
            case 'b': ch = '\b'; break;
            case 'f': ch = '\f'; break;
            case 'n': ch = '\n'; break;
            case 'r': ch = '\r'; break;
            case 't': ch = '\t'; break;
            case 'v': ch = '\v'; break;
            default:
                if (check_char_in(ls, "\'\"\\")) {
                    ch = ls->current;
                    break; // Go to save and skip.
                } 
                // Invalid escape sequence.
                return error_token(ls);
            }
            // Append the escaped character, then skip the literal one in view.
            save_char(ls, ch);
            skip_char(ls);
        } else if (check_char_in(ls, "\r\n") || is_eof(ls)) {
            return error_token(ls);
        } else {
            consume_char(ls);
        }
    } while (!match_char(ls, q));
    Token t = make_token(ls, Type::String);
    t.data.string = buffer_to_string(ls, 1, -2);
    return t;
}

static void consume_number(Lexer *ls)
{
    for (;;) {
        do {
            consume_char(ls);
        } while (isdigit(ls->current));
        
        // Have an exponent?
        if (match_char_in(ls, "Ee")) {
            // Have explicit signedness? (optional)
            match_char_in(ls, "+-");
            continue;
        }
        // Have a decimal point?
        if (match_char(ls, '.')) {
            continue;
        }
        // None of the above conditions passed so break out of here.
        break;
    }
    // Consume any remaining stuff. This may also consume hexadecimal.
    while (isalnum(ls->current) || ls->current == '_') {
        consume_char(ls);
    }
}

static Token number_token(Lexer *ls)
{
    consume_number(ls);
    Token   t = make_token(ls, Type::Number);
    String *s = buffer_to_string(ls); // intern string representation already.
    char   *end;
    Number  n = std::strtod(s->data, &end);
    // Failed to convert the entire lexeme?
    if (end != (s->data + s->length))
        return error_token(ls);
    t.data.number = n;
    return t;
}

static void adjust_token(Token *t, String *s, Type type)
{
    std::string_view v = token_to_string(type);
    if (v.length() == s->length && memcmp(v.data(), s->data, s->length) == 0)
        t->type = type;
}

static void adjust_if_keyword(Token *t, String *s)
{
    switch (s->data[0]) {
    case 'a': adjust_token(t, s, Type::And);   break;
    case 'b': adjust_token(t, s, Type::Break); break;
    case 'd': adjust_token(t, s, Type::Do);    break;
    case 'e':
        // "end", "else", "elseif"
        switch (s->length) {
        case 3: adjust_token(t, s, Type::End);    break;
        case 4: adjust_token(t, s, Type::Else);   break;
        case 6: adjust_token(t, s, Type::ElseIf); break;
        }
        break;
    case 'f':
        // "for", "false", "function"
        switch (s->length) {
        case 3: adjust_token(t, s, Type::For);      break;
        case 5: adjust_token(t, s, Type::False);    break;
        case 8: adjust_token(t, s, Type::Function); break;
        }
        break;
    case 'i':
        if (s->length != 2)
            break;
        // "if" and "in"
        switch (s->data[1]) {
        case 'f': adjust_token(t, s, Type::If); break;
        case 'n': adjust_token(t, s, Type::In); break;
        }
        break;
    case 'l': adjust_token(t, s, Type::Local); break;
    case 'n':
        if (s->length != 3)
            break;
        // "nil" and "not"
        switch (s->data[1]) {
        case 'i': adjust_token(t, s, Type::Nil); break;
        case 'o': adjust_token(t, s, Type::Not); break;
        }
        break;
    case 'o': adjust_token(t, s, Type::Or); break;
    case 'r':
        if (s->length != 6)
            break;
        // "repeat" and "return" have the same index 0 and 1, so check index 2.
        switch (s->data[2]) {
        case 't': adjust_token(t, s, Type::Return); break;
        case 'p': adjust_token(t, s, Type::Repeat); break;
        }
        break;
    case 't':
        if (s->length != 4)
            break;
        // "then" and "true"
        switch (s->data[1]) {
        case 'h': adjust_token(t, s, Type::Then); break;
        case 'r': adjust_token(t, s, Type::True); break;
        }
    case 'u': adjust_token(t, s, Type::Until); break;
    case 'w': adjust_token(t, s, Type::While); break;
    }
    
}

static Token keyword_or_identifier_token(Lexer *ls)
{
    // Consume first letter until we hit a non-identifier.
    do {
        consume_char(ls);
    } while (isalnum(ls->current) || ls->current == '_');
    
    Token   t = make_token(ls, Type::Identifier);
    String *s = buffer_to_string(ls);
    adjust_if_keyword(&t, s);
    t.data.string = s;
    return t;
}

static Token make_token_if(Lexer *ls, char ch, Token::Type y, Token::Type n)
{
    bool found = match_char(ls, ch);
    // Used only by TildeEq.
    if (!found && n == Type::Error)
        return error_token(ls);
    return make_token(ls, found ? y : n);
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

// Creates a token including the currently viewing character.
static Token consume_token(Lexer *ls, Token::Type t)
{
    consume_char(ls);
    return make_token(ls, t);
}

Token scan_token(Lexer *ls)
{
    reset_buffer(ls->buffer);
    skip_whitespace(ls);
    
    if (is_eof(ls))
        return make_token(ls, Type::Eof);
    
    // Save now as ls->current will likely get updated as we move along.
    char ch = ls->current;
    if (isdigit(ch))
        return number_token(ls);
    if (isalpha(ch) || ch == '_')
        return keyword_or_identifier_token(ls);

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
    case '\"': return string_token(ls, ch);
    }
    // Save whatever that was so we can report it.
    consume_char(ls);
    return error_token(ls);
}

template<class K, class V, size_t N>
class LookupTable {
public:
    // Read-write element access.
    constexpr V& operator[](K key)
    {
        return m_table[cast<size_t>(key)];
    }
    // Read-only element access.
    constexpr const V& operator[](K key) const
    {
        return m_table[cast<size_t>(key)];
    }
private:
    std::array<V, N> m_table;
};

// Constexpr lambdas are a C++17 thing.
// We do it this way because C++ does not allow designated initializers...
static constexpr auto TOKEN_TO_STRING = []() constexpr
{
    using K = Token::Type;
    using V = struct {
        std::string_view name;
        std::string_view display;
    };
    LookupTable<K, V, TOKEN_COUNT> t;
    // Keywords
    t[K::And]      = {"And",      "and"},
    t[K::Break]    = {"Break",    "break"},
    t[K::Do]       = {"Do",       "do"},
    t[K::Else]     = {"Else",     "else"},
    t[K::ElseIf]   = {"ElseIf",   "elseif"},
    t[K::End]      = {"End",      "end"},
    t[K::False]    = {"False",    "false"},
    t[K::For]      = {"For",      "for"},
    t[K::Function] = {"Function", "function"},
    t[K::If]       = {"If",       "if"},
    t[K::In]       = {"In",       "in"},
    t[K::Local]    = {"Local",    "local"},
    t[K::Nil]      = {"Nil",      "nil"},
    t[K::Not]      = {"Not",      "not"},
    t[K::Or]       = {"Or",       "or"},
    t[K::Repeat]   = {"Repeat",   "repeat"},
    t[K::Return]   = {"Return",   "return"},
    t[K::Then]     = {"Then",     "then"},
    t[K::True]     = {"True",     "true"},
    t[K::Until]    = {"Until",    "until"},
    t[K::While]    = {"While",    "while"},

    // Brackets
    t[K::LParen]   = {"LParen",   "("},
    t[K::RParen]   = {"RParen",   ")"},
    t[K::LSquare]  = {"LSquare",  "["},
    t[K::RSquare]  = {"RSquare",  "]"},
    t[K::LCurly]   = {"LCurly",   "{"},
    t[K::RCurly]   = {"RCurly",   "}"},

    // Relational Operators
    t[K::LAngle]   = {"LAngle",   "<"},
    t[K::RAngle]   = {"RAngle",   ">"},
    t[K::LAngleEq] = {"LAngleEq", "<="},
    t[K::RAngleEq] = {"RAngleEq", ">="},
    t[K::Equal1]   = {"Equal1",   "="},
    t[K::Equal2]   = {"Equal2",   "=="},
    t[K::TildeEq]  = {"TildeEq",  "~="},

    // Punctuation
    t[K::Dot1]     = {"Dot1", "."},
    t[K::Dot2]     = {"Dot2", ".."}, 
    t[K::Dot3]     = {"Dot3", "..."},
    t[K::Comma]    = {"Comma", ","},
    t[K::Colon]    = {"Colon", ":"},
    t[K::Semicolon] = {"Semicolon", ";"},

    // Arithmetic Operators
    t[K::Plus]     = {"Plus", "+"},
    t[K::Dash]     = {"Dash", "-"},
    t[K::Star]     = {"Star", "*"},
    t[K::Slash]    = {"Slash", "/"},
    t[K::Percent]  = {"Percent", "%"},
    t[K::Caret]    = {"Caret", "/"},

    // Other
    t[K::Identifier] = {"Identifier", "<identifier>"},
    t[K::String]     = {"String", "<string>"},
    t[K::Number]     = {"Number", "<number>"},
    t[K::Error]      = {"Error", "<error>"},
    t[K::Eof]        = {"Eof", "<eof>"};
    return t;
}();

static std::string_view token_to_string(Token::Type t)
{
    return TOKEN_TO_STRING[t].display;
}

const char *name_token(Token::Type t)
{
    return TOKEN_TO_STRING[t].name.data();
}

const char *display_token(Token::Type t)
{
    return TOKEN_TO_STRING[t].display.data();
}
