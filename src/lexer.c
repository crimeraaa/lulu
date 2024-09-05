#include "lexer.h"

#include <ctype.h>
#include <string.h>
#include <stdio.h>

void lulu_Lexer_init(lulu_Lexer *self, cstring input)
{
    self->start   = input;
    self->current = input;
    self->line    = 1;
}

static bool lexer_is_at_end(const lulu_Lexer *self)
{
    return self->current[0] == '\0';
}

/**
 * @brief
 *      Retrieve the current character being pointed to then advance the
 *      `current` pointer.
 *      
 * @note 2024-09-05
 *      Analogous to the book's `scanner.c:advance()`.
 */
static char lexer_consume_char(lulu_Lexer *self)
{
    return *self->current++;
}

static char lexer_peek_char(const lulu_Lexer *self)
{
    return self->current[0];
}

static char lexer_peek_next_char(const lulu_Lexer *self)
{
    // Dereferencing 1 past current may be illegal?
    if (lexer_is_at_end(self)) {
        return '\0';
    } 
    return self->current[1];
}

/**
 * @brief
 *      If the current character being pointed to matches `expected`, advance
 *      and return true. Otherwise return do nothing and false.
 */
static bool lexer_match_char(lulu_Lexer *self, char expected)
{
    if (!lexer_is_at_end(self) && *self->current == expected) {
        self->current++;
        return true;
    }
    return false;
}

static bool lexer_match_char_any(lulu_Lexer *self, cstring charset)
{
    if (strchr(charset, lexer_peek_char(self))) {
        self->current++;
        return true;
    }
    return false;
}

static lulu_Token lexer_make_token(const lulu_Lexer *self, lulu_Token_Type type)
{
    lulu_Token token;
    token.type        = type;
    token.string.data = self->start;
    token.string.len  = self->current - self->start;
    token.line        = self->line;
    return token;
}

static lulu_Token lexer_error_token(const lulu_Lexer *self, cstring msg)
{
    lulu_Token token;
    token.type        = TOKEN_ERROR;
    token.string.data = msg;
    token.string.len  = cast(isize)strlen(msg);
    token.line        = self->line;
    return token;    
}

static void lexer_skip_multiline(lulu_Lexer *self, int opening)
{
    for (;;) {
        if (lexer_match_char(self, ']')) {
            int closing = 0;
            while (lexer_match_char(self, '=')) {
                closing++;
            }
            if (closing == opening && lexer_match_char(self, ']')) {
                return;
            }
        }
        /**
         * @brief
         *      Above call may have fallen through to here as well.
         *
         * @todo 2024-09-05
         *      Throw error if at EOF!
         */
        if (lexer_is_at_end(self)) {
            return;
        }
        if (lexer_peek_char(self) == '\n') {
            self->line++;
        }
        lexer_consume_char(self);
    }
}

/**
 * @brief
 *      Assumed we already consumed 2 consecutive '-' characters and we're
 *      pointing at the first character in the comment body, or a '['.
 */
static void lexer_skip_comment(lulu_Lexer *self)
{
    if (lexer_match_char(self, '[')) {
        int opening = 0;
        while (lexer_match_char(self, '=')) {
            opening++;
        }
        if (lexer_match_char(self, '[')) {
            lexer_skip_multiline(self, opening);
            return;
        }
    }
    // Skip a single line comment.
    while (lexer_peek_char(self) != '\n' && !lexer_is_at_end(self)) {
        lexer_consume_char(self);
    }
}

static void lexer_skip_whitespace(lulu_Lexer *self)
{
    for (;;) {
        char ch = lexer_peek_char(self);
        switch (ch) {
        case '\n': self->line++; // fall through
        case ' ':
        case '\r':
        case '\t':
            lexer_consume_char(self);
            break;
        case '-':
            // Don't have 2 '-' consecutively?
            if (lexer_peek_next_char(self) != '-') {
                return;
            }
            // Skip the 2 dashes.
            lexer_consume_char(self);
            lexer_consume_char(self);
            lexer_skip_comment(self);
            break;
        default:
            return;
        }
    }
}

#define str_lit(c_str) {(c_str), size_of(c_str) - 1}

const String LULU_KEYWORDS[LULU_KEYWORD_COUNT] = {
    [TOKEN_AND]         = str_lit("and"),
    [TOKEN_BREAK]       = str_lit("break"),
    [TOKEN_DO]          = str_lit("do"),
    [TOKEN_ELSE]        = str_lit("else"),
    [TOKEN_ELSEIF]      = str_lit("elseif"),
    [TOKEN_END]         = str_lit("end"),
    [TOKEN_FALSE]       = str_lit("false"),
    [TOKEN_FOR]         = str_lit("for"),
    [TOKEN_FUNCTION]    = str_lit("function"),
    [TOKEN_IF]          = str_lit("if"),
    [TOKEN_IN]          = str_lit("in"),
    [TOKEN_LOCAL]       = str_lit("local"),
    [TOKEN_NIL]         = str_lit("nil"),
    [TOKEN_NOT]         = str_lit("not"),
    [TOKEN_OR]          = str_lit("or"),
    [TOKEN_PRINT]       = str_lit("print"),
    [TOKEN_RETURN]      = str_lit("return"),
    [TOKEN_THEN]        = str_lit("then"),
    [TOKEN_TRUE]        = str_lit("true"),
    [TOKEN_WHILE]       = str_lit("while"),
};

#undef str_lit

static lulu_Token_Type keyword_check_type(String current, lulu_Token_Type type)
{
    const String keyword = LULU_KEYWORDS[type];
    if (keyword.len == current.len) {
        if (memcmp(keyword.data, current.data, keyword.len) == 0) {
            return type;
        }
    }
    
    return TOKEN_IDENTIFIER;
}

static lulu_Token_Type lexer_get_identifier_type(lulu_Lexer *self)
{
    String current = {self->start, self->current - self->start};
    switch (current.data[0]) {
    case 'a': return keyword_check_type(current, TOKEN_AND);
    case 'b': return keyword_check_type(current, TOKEN_BREAK);
    case 'd': return keyword_check_type(current, TOKEN_DO);
    case 'e':
        switch (current.len) {
        case 3: return keyword_check_type(current, TOKEN_END);
        case 4: return keyword_check_type(current, TOKEN_ELSE);
        case 6: return keyword_check_type(current, TOKEN_ELSEIF);
        }
        break;
    case 'f':
        if (current.len < 3) {
            break;
        }
        switch (current.data[1]) {
        case 'o': return keyword_check_type(current, TOKEN_FOR);
        case 'u': return keyword_check_type(current, TOKEN_FUNCTION);
        }
        break;
    case 'i':
        if (current.len != 2) {
            break;
        }
        switch (current.data[1]) {
        case 'f': return keyword_check_type(current, TOKEN_IF);
        case 'i': return keyword_check_type(current, TOKEN_IN);
        }
        break;
    case 'l': return keyword_check_type(current, TOKEN_LOCAL);
    case 'n':
        if (current.len != 3) {
            break;
        }
        switch (current.data[1]) {
        case 'i': return keyword_check_type(current, TOKEN_NIL);
        case 'o': return keyword_check_type(current, TOKEN_NOT);
        }
        break;
    case 'o': return keyword_check_type(current, TOKEN_OR);
    case 'r': return keyword_check_type(current, TOKEN_RETURN);
    case 't':
        if (current.len != 4) {
            break;
        }
        switch (current.data[1]) {
        case 'h': return keyword_check_type(current, TOKEN_THEN);
        case 'r': return keyword_check_type(current, TOKEN_TRUE);
        }
        break;
    case 'w': return keyword_check_type(current, TOKEN_WHILE);        
    }
    return TOKEN_IDENTIFIER;
}

static lulu_Token lexer_consume_identifier(lulu_Lexer *self)
{
    while (isalnum(lexer_peek_char(self)) || lexer_peek_char(self) == '_') {
        lexer_consume_char(self);
    }
    return lexer_make_token(self, lexer_get_identifier_type(self));
}

static void lexer_consume_base10(lulu_Lexer *self)
{
    while (isdigit(lexer_peek_char(self))) {
        lexer_consume_char(self);
    }
}

static void lexer_consume_base16(lulu_Lexer *self)
{
    while (isxdigit(lexer_peek_char(self))) {
        lexer_consume_char(self);
    }
}

static lulu_Token lexer_consume_number(lulu_Lexer *self)
{
    if (lexer_match_char(self, '0')) {
        if (lexer_match_char_any(self, "xX")) {
            lexer_consume_base16(self);
        }
    }
    lexer_consume_base10(self);
    
    // Consume fractional segment with 0 or more digits.
    if (lexer_match_char(self, '.')) {
        lexer_consume_base10(self);
    }
    
    // Consume exponent segment with optional sign and 1 or more digits.
    if (lexer_match_char_any(self, "eE")) {
        lexer_match_char_any(self, "+-");
        lexer_consume_base10(self);
    }
    
    // Error handling will be done later.
    while (isalnum(lexer_peek_char(self)) || lexer_peek_char(self) == '_') {
        lexer_consume_char(self);
    }

    return lexer_make_token(self, TOKEN_NUMBER_LIT);
}

static lulu_Token lexer_consume_string(lulu_Lexer *self, char quote)
{
    while (lexer_peek_char(self) != quote && !lexer_is_at_end(self)) {
        if (lexer_peek_char(self) == '\n') {
            goto unterminated_string;
        }
        lexer_consume_char(self);
    }
    
    if (lexer_is_at_end(self)) {
unterminated_string: 
        return lexer_error_token(self, "Unterminated string");
    }
    
    // Consume closing quote.
    lexer_consume_char(self);
    return lexer_make_token(self, TOKEN_STRING_LIT);
}

lulu_Token lulu_Lexer_scan_token(lulu_Lexer *self)
{
    lexer_skip_whitespace(self);
    self->start = self->current;
    
    if (lexer_is_at_end(self)) {
        return lexer_make_token(self, TOKEN_EOF);
    }
    
    char ch = lexer_consume_char(self);
    if (isdigit(ch)) {
        return lexer_consume_number(self);
    }
    if (isalpha(ch) || ch == '_') {
        return lexer_consume_identifier(self);
    }

    switch (ch) {
    case '(': return lexer_make_token(self, TOKEN_PAREN_L);
    case ')': return lexer_make_token(self, TOKEN_PAREN_R);
    case '{': return lexer_make_token(self, TOKEN_CURLY_L);
    case '}': return lexer_make_token(self, TOKEN_CURLY_R);
    case '[': return lexer_make_token(self, TOKEN_BRACKET_L);
    case ']': return lexer_make_token(self, TOKEN_BRACKET_R);

    case ',': return lexer_make_token(self, TOKEN_COMMA);
    case ':': return lexer_make_token(self, TOKEN_COLON);
    case ';': return lexer_make_token(self, TOKEN_SEMICOLON);
    case '.':
        if (lexer_match_char(self, '.')) {
            if (lexer_match_char(self, '.')) {
                return lexer_make_token(self, TOKEN_ELLIPSIS_3);
            }
            return lexer_make_token(self, TOKEN_ELLIPSIS_2);
        }
        return lexer_make_token(self, TOKEN_PERIOD);
    case '#': return lexer_make_token(self, TOKEN_HASH);

    case '+': return lexer_make_token(self, TOKEN_PLUS);
    case '-': return lexer_make_token(self, TOKEN_DASH);
    case '*': return lexer_make_token(self, TOKEN_ASTERISK);
    case '/': return lexer_make_token(self, TOKEN_SLASH);
    
    case '~':
        if (lexer_match_char(self, '=')) {
            return lexer_make_token(self, TOKEN_TILDE_EQUAL);
        }
        return lexer_error_token(self, "Expected '=' after '~'");
    case '=':
        return lexer_make_token(
            self,
            lexer_match_char(self, '=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
    case '<':
        return lexer_make_token(
            self,
            lexer_match_char(self, '=') ? TOKEN_ANGLE_EQUAL_L : TOKEN_ANGLE_L);
    case '>':
        return lexer_make_token(
            self,
            lexer_match_char(self, '=') ? TOKEN_ANGLE_EQUAL_R : TOKEN_ANGLE_R);
    
    case '\'':
    case '\"': return lexer_consume_string(self, ch);
    }

    return lexer_error_token(self, "Unexpected character.");
}
