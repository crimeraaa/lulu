#include "lexer.h"
#include "vm.h"

#include <string.h>

static bool
is_ascii_digit(char ch)
{
    return '0' <= ch && ch <= '9';
}

static bool
is_ascii_hexdigit(char ch)
{
    return is_ascii_digit(ch) || ('a' <= ch && ch <= 'f') || ('A' <= ch && ch <= 'F');
}

static bool
is_ascii_alpha(char ch)
{
    return ('a' <= ch && ch <= 'z') || ('A' <= ch && ch <= 'Z') || ch == '_';
}

static bool
is_ascii_alnum(char ch)
{
    return is_ascii_alpha(ch) || is_ascii_digit(ch);
}

void
lulu_Lexer_init(lulu_VM *vm, lulu_Lexer *self, cstring filename, cstring input)
{
    self->vm       = vm;
    self->filename = filename;
    self->string   = NULL;
    self->start    = input;
    self->current  = input;
    self->line     = 1;
}

static bool
is_at_end(const lulu_Lexer *lexer)
{
    return lexer->current[0] == '\0';
}

/**
 * @brief
 *      Retrieve the current character being pointed to then advance the
 *      `current` pointer.
 *      
 * @note 2024-09-05
 *      Analogous to the book's `scanner.c:advance()`.
 */
static char
advance_char(lulu_Lexer *lexer)
{
    return *lexer->current++;
}

static char
peek_char(const lulu_Lexer *lexer)
{
    return lexer->current[0];
}

static char
peek_next_char(const lulu_Lexer *lexer)
{
    // Dereferencing 1 past current may be illegal?
    if (is_at_end(lexer)) {
        return '\0';
    } 
    return lexer->current[1];
}

/**
 * @brief
 *      If the current character being pointed to matches `expected`, advance
 *      and return true. Otherwise return do nothing and false.
 */
static bool
match_char(lulu_Lexer *lexer, char expected)
{
    if (!is_at_end(lexer) && *lexer->current == expected) {
        lexer->current++;
        return true;
    }
    return false;
}

static bool
match_char_any(lulu_Lexer *lexer, cstring charset)
{
    if (strchr(charset, peek_char(lexer))) {
        lexer->current++;
        return true;
    }
    return false;
}

static lulu_Token
make_token(const lulu_Lexer *lexer, lulu_Token_Type type)
{
    lulu_Token token;
    token.type        = type;
    token.lexeme.data = lexer->start;
    token.lexeme.len  = lexer->current - lexer->start;
    token.line        = lexer->line;
    return token;
}

/**
 * @warning 2024-09-07
 *      Actually since this function throws an error, it does not return!
 */
noreturn static void
error_token(const lulu_Lexer *lexer, cstring msg)
{
    lulu_Token token = make_token(lexer, TOKEN_ERROR);
    lulu_VM_comptime_error(lexer->vm, lexer->filename, token.line, msg, token.lexeme);
}

static void
skip_multiline(lulu_Lexer *lexer, int opening)
{
    for (;;) {
        if (match_char(lexer, ']')) {
            int closing = 0;
            while (match_char(lexer, '=')) {
                closing++;
            }
            if (closing == opening && match_char(lexer, ']')) {
                return;
            }
        }
        /**
         * @brief
         *      Above call may have fallen through to here as well.
         */
        if (is_at_end(lexer)) {
            error_token(lexer, "Unterminated multiline comment");
            return;
        }
        if (peek_char(lexer) == '\n') {
            lexer->line++;
        }
        advance_char(lexer);
    }
}

/**
 * @brief
 *      Assumed we already consumed 2 consecutive '-' characters and we're
 *      pointing at the first character in the comment body, or a '['.
 */
static void
skip_comment(lulu_Lexer *lexer)
{
    if (match_char(lexer, '[')) {
        int opening = 0;
        while (match_char(lexer, '=')) {
            opening++;
        }
        if (match_char(lexer, '[')) {
            skip_multiline(lexer, opening);
            return;
        }
    }
    // Skip a single line comment.
    while (peek_char(lexer) != '\n' && !is_at_end(lexer)) {
        advance_char(lexer);
    }
}

static void
skip_whitespace(lulu_Lexer *lexer)
{
    for (;;) {
        char ch = peek_char(lexer);
        switch (ch) {
        case '\n': lexer->line++; // fall through
        case ' ':
        case '\r':
        case '\t':
            advance_char(lexer);
            break;
        case '-':
            // Don't have 2 '-' consecutively?
            if (peek_next_char(lexer) != '-') {
                return;
            }
            // Skip the 2 dashes.
            advance_char(lexer);
            advance_char(lexer);
            skip_comment(lexer);
            break;
        default:
            return;
        }
    }
}

#define lit String_literal

const String LULU_KEYWORDS[LULU_KEYWORD_COUNT] = {
    [TOKEN_AND]         = lit("and"),
    [TOKEN_BREAK]       = lit("break"),
    [TOKEN_DO]          = lit("do"),
    [TOKEN_ELSE]        = lit("else"),
    [TOKEN_ELSEIF]      = lit("elseif"),
    [TOKEN_END]         = lit("end"),
    [TOKEN_FALSE]       = lit("false"),
    [TOKEN_FOR]         = lit("for"),
    [TOKEN_FUNCTION]    = lit("function"),
    [TOKEN_IF]          = lit("if"),
    [TOKEN_IN]          = lit("in"),
    [TOKEN_LOCAL]       = lit("local"),
    [TOKEN_NIL]         = lit("nil"),
    [TOKEN_NOT]         = lit("not"),
    [TOKEN_OR]          = lit("or"),
    [TOKEN_PRINT]       = lit("print"),
    [TOKEN_REPEAT]      = lit("repeat"),
    [TOKEN_RETURN]      = lit("return"),
    [TOKEN_THEN]        = lit("then"),
    [TOKEN_TRUE]        = lit("true"),
    [TOKEN_UNTIL]       = lit("until"),
    [TOKEN_WHILE]       = lit("while"),
};

#undef lit

static lulu_Token_Type
check_keyword(String current, lulu_Token_Type type)
{
    const String keyword = LULU_KEYWORDS[type];
    if (keyword.len == current.len) {
        if (memcmp(keyword.data, current.data, keyword.len) == 0) {
            return type;
        }
    }
    return TOKEN_IDENTIFIER;
}

static lulu_Token_Type
get_identifier_type(lulu_Lexer *lexer)
{
    String current = {lexer->start, lexer->current - lexer->start};
    switch (current.data[0]) {
    case 'a': return check_keyword(current, TOKEN_AND);
    case 'b': return check_keyword(current, TOKEN_BREAK);
    case 'd': return check_keyword(current, TOKEN_DO);
    case 'e':
        switch (current.len) {
        case 3: return check_keyword(current, TOKEN_END);
        case 4: return check_keyword(current, TOKEN_ELSE);
        case 6: return check_keyword(current, TOKEN_ELSEIF);
        }
        break;
    case 'f':
        if (current.len < 3) {
            break;
        }
        switch (current.data[1]) {
        case 'a': return check_keyword(current, TOKEN_FALSE);
        case 'o': return check_keyword(current, TOKEN_FOR);
        case 'u': return check_keyword(current, TOKEN_FUNCTION);
        }
        break;
    case 'i':
        if (current.len != 2) {
            break;
        }
        switch (current.data[1]) {
        case 'f': return check_keyword(current, TOKEN_IF);
        case 'i': return check_keyword(current, TOKEN_IN);
        }
        break;
    case 'l': return check_keyword(current, TOKEN_LOCAL);
    case 'n':
        if (current.len != 3) {
            break;
        }
        switch (current.data[1]) {
        case 'i': return check_keyword(current, TOKEN_NIL);
        case 'o': return check_keyword(current, TOKEN_NOT);
        }
        break;
    case 'o': return check_keyword(current, TOKEN_OR);
    case 'r': 
        if (current.len < 3) {
            break;
        }
        switch (current.data[2]) {
        case 'p': return check_keyword(current, TOKEN_REPEAT);
        case 't': return check_keyword(current, TOKEN_RETURN);
        }
    case 't':
        if (current.len != 4) {
            break;
        }
        switch (current.data[1]) {
        case 'h': return check_keyword(current, TOKEN_THEN);
        case 'r': return check_keyword(current, TOKEN_TRUE);
        }
        break;
    case 'u': return check_keyword(current, TOKEN_UNTIL);
    case 'w': return check_keyword(current, TOKEN_WHILE);        
    }
    return TOKEN_IDENTIFIER;
}

static lulu_Token
consume_identifier(lulu_Lexer *lexer)
{
    while (is_ascii_alnum(peek_char(lexer)) || peek_char(lexer) == '_') {
        advance_char(lexer);
    }
    return make_token(lexer, get_identifier_type(lexer));
}

static void
consume_base10(lulu_Lexer *lexer)
{
    while (is_ascii_digit(peek_char(lexer))) {
        advance_char(lexer);
    }
}

static void
consume_base16(lulu_Lexer *lexer)
{
    while (is_ascii_hexdigit(peek_char(lexer))) {
        advance_char(lexer);
    }
}

static lulu_Token
consume_number(lulu_Lexer *lexer)
{
    if (match_char(lexer, '0')) {
        if (match_char_any(lexer, "xX")) {
            consume_base16(lexer);
            goto trailing_characters;
        }
    }
    consume_base10(lexer);
    
    // Consume fractional segment with 0 or more digits.
    if (match_char(lexer, '.')) {
        consume_base10(lexer);
    }
    
    // Consume exponent segment with optional sign and 1 or more digits.
    if (match_char_any(lexer, "eE")) {
        match_char_any(lexer, "+-");
        consume_base10(lexer);
    }
    
    // Error handling will be done later.
trailing_characters:
    while (is_ascii_alnum(peek_char(lexer)) || peek_char(lexer) == '_') {
        advance_char(lexer);
    }

    return make_token(lexer, TOKEN_NUMBER_LIT);
}

static int
get_escape_char(char ch)
{
    switch (ch) {
    case '0':  return '\0';
    case 'a':  return '\a';
    case 'b':  return '\b';
    case 't':  return '\t';
    case 'n':  return '\n';
    case 'v':  return '\v';
    case 'f':  return '\f';
    case 'r':  return '\r';
              
    case '\"': return '\"';
    case '\'': return '\'';
    case '\\': return '\\';

    default:   return -1;
    }
}

static lulu_Token
consume_string(lulu_Lexer *lexer, char quote)
{
    lulu_String_Builder *builder = &lexer->vm->builder;
    lulu_String_Builder_reset(builder);
    for (;;) {
        char ch = peek_char(lexer);
        if (ch == quote || is_at_end(lexer)) {
            break;
        }

        if (ch == '\n') {
            goto unterminated_string;
        }

        if (ch == '\\') {
            int esc = get_escape_char(peek_next_char(lexer));
            if (esc == -1) {
                error_token(lexer, "Invalid escape character");
            }
            ch = cast(char)esc;
            advance_char(lexer);
        }
        lulu_String_Builder_write_char(builder, ch);
        advance_char(lexer);
    }
    
    if (is_at_end(lexer)) {
unterminated_string: 
        error_token(lexer, "Unterminated string");
    }
    
    // Consume closing quote.
    advance_char(lexer);
    lexer->string = lulu_String_Builder_to_string(builder);
    return make_token(lexer, TOKEN_STRING_LIT);
}

lulu_Token
lulu_Lexer_scan_token(lulu_Lexer *self)
{
    skip_whitespace(self);
    self->start = self->current;
    
    if (is_at_end(self)) {
        return make_token(self, TOKEN_EOF);
    }
    
    char ch = advance_char(self);
    if (is_ascii_digit(ch)) {
        return consume_number(self);
    }
    if (is_ascii_alpha(ch) || ch == '_') {
        return consume_identifier(self);
    }

    switch (ch) {
    case '(': return make_token(self, TOKEN_PAREN_L);
    case ')': return make_token(self, TOKEN_PAREN_R);
    case '{': return make_token(self, TOKEN_CURLY_L);
    case '}': return make_token(self, TOKEN_CURLY_R);
    case '[': return make_token(self, TOKEN_BRACKET_L);
    case ']': return make_token(self, TOKEN_BRACKET_R);

    case ',': return make_token(self, TOKEN_COMMA);
    case ':': return make_token(self, TOKEN_COLON);
    case ';': return make_token(self, TOKEN_SEMICOLON);
    case '.':
        if (match_char(self, '.')) {
            if (match_char(self, '.')) {
                return make_token(self, TOKEN_ELLIPSIS_3);
            }
            return make_token(self, TOKEN_ELLIPSIS_2);
        }
        return make_token(self, TOKEN_PERIOD);
    case '#': return make_token(self, TOKEN_HASH);

    case '+': return make_token(self, TOKEN_PLUS);
    case '-': return make_token(self, TOKEN_DASH);
    case '*': return make_token(self, TOKEN_ASTERISK);
    case '/': return make_token(self, TOKEN_SLASH);
    case '%': return make_token(self, TOKEN_PERCENT);
    case '^': return make_token(self, TOKEN_CARET);
    
    case '~':
        if (match_char(self, '=')) {
            return make_token(self, TOKEN_TILDE_EQUAL);
        }
        error_token(self, "Expected '=' after '~'");
    case '=':
        return make_token(self, match_char(self, '=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
    case '<':
        return make_token(self, match_char(self, '=') ? TOKEN_ANGLE_L_EQUAL : TOKEN_ANGLE_L);
    case '>':
        return make_token(self, match_char(self, '=') ? TOKEN_ANGLE_R_EQUAL : TOKEN_ANGLE_R);
    
    case '\'':
    case '\"': return consume_string(self, ch);
    }

    error_token(self, "Unexpected character");
}
