/// local
#include "lexer.h"
#include "vm.h"

/// standard
#include <stdlib.h> // strtod
#include <string.h> // strchr, memcmp

static inline bool
is_digit(char ch)
{
    return '0' <= ch && ch <= '9';
}

static inline bool
is_lower(char ch)
{
    return 'a' <= ch && ch <= 'z';
}

static inline bool
is_upper(char ch)
{
    return 'A' <= ch && ch <= 'Z';
}

static inline bool
is_hexdigit(char ch)
{
    return is_digit(ch) || ('a' <= ch && ch <= 'f') || ('A' <= ch && ch <= 'F');
}

static inline bool
is_alpha(char ch)
{
    return is_upper(ch) || is_lower(ch) || ch == '_';
}

static inline bool
is_alnum(char ch)
{
    return is_alpha(ch) || is_digit(ch);
}

void
token_init(Token *self, const char *start, isize len, Token_Type type, int line)
{
    self->start = start;
    self->len   = len;
    self->type  = type;
    self->line  = line;
}

void
token_init_empty(Token *self)
{
    token_init(self, NULL, 0, cast(Token_Type)0, 0);
}

void
lexer_init(lulu_VM *vm, Lexer *self, cstring filename, const char *input, isize len)
{
    self->vm       = vm;
    self->filename = filename;
    self->string   = NULL;
    self->start    = input;
    self->current  = input;
    self->end      = input + len;
    self->line     = 1;
}

static inline bool
is_at_end(const Lexer *lexer)
{
    return lexer->current >= lexer->end;
}

/**
 * @brief
 *      Retrieve the current character being pointed to then advance the
 *      `current` pointer.
 *
 * @note 2024-09-05
 *      Analogous to the book's `scanner.c:advance()`.
 */
static inline char
advance_char(Lexer *lexer)
{
    return *lexer->current++;
}

static inline char
peek_char(const Lexer *lexer)
{
    return lexer->current[0];
}

static char
peek_next_char(const Lexer *lexer)
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
match_char(Lexer *lexer, char expected)
{
    if (!is_at_end(lexer) && *lexer->current == expected) {
        lexer->current++;
        return true;
    }
    return false;
}

static bool
match_char_any(Lexer *lexer, cstring charset)
{
    if (strchr(charset, peek_char(lexer))) {
        lexer->current++;
        return true;
    }
    return false;
}

static Token
make_token(const Lexer *lexer, Token_Type type)
{
    Token       token;
    const char *start  = lexer->start;
    isize       len    = lexer->current - lexer->start;
    token_init(&token, start, len, type, lexer->line);
    return token;
}

LULU_ATTR_NORETURN
static void
error_token(const Lexer *lexer, cstring msg)
{
    Token       token = make_token(lexer, TOKEN_ERROR);
    const char *where = token.start;
    isize       len   = token.len;
    vm_comptime_error(lexer->vm, lexer->filename, token.line, msg, where, len);
}

/**
 * @brief
 *      Count the number of consecutive '=' immediately following a '['.
 *
 * @note 2024-09-17
 *      Assumes we just consumed a '[' character.
 */
static int
get_nesting(Lexer *lexer)
{
    int nesting = 0;
    while (match_char(lexer, '=')) {
        nesting++;
    }
    return nesting;
}

static void
skip_multiline(Lexer *lexer, int opening)
{
    for (;;) {
        if (match_char(lexer, ']')) {
            int closing = get_nesting(lexer);
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
skip_comment(Lexer *lexer)
{
    if (match_char(lexer, '[')) {
        int opening = get_nesting(lexer);
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
skip_whitespace(Lexer *lexer)
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

#define lit(cstr)   {cstr, size_of(cstr) - 1}

// @todo 2024-09-22: Just remove the designated initializers entirely!
#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wc99-designator"
#endif

const LString
LULU_TOKEN_STRINGS[LULU_TOKEN_COUNT] = {
    [TOKEN_AND]           = lit("and"),
    [TOKEN_BREAK]         = lit("break"),
    [TOKEN_DO]            = lit("do"),
    [TOKEN_ELSE]          = lit("else"),
    [TOKEN_ELSEIF]        = lit("elseif"),
    [TOKEN_END]           = lit("end"),
    [TOKEN_FALSE]         = lit("false"),
    [TOKEN_FOR]           = lit("for"),
    [TOKEN_FUNCTION]      = lit("function"),
    [TOKEN_IF]            = lit("if"),
    [TOKEN_IN]            = lit("in"),
    [TOKEN_LOCAL]         = lit("local"),
    [TOKEN_NIL]           = lit("nil"),
    [TOKEN_NOT]           = lit("not"),
    [TOKEN_OR]            = lit("or"),
    [TOKEN_PRINT]         = lit("print"),
    [TOKEN_REPEAT]        = lit("repeat"),
    [TOKEN_RETURN]        = lit("return"),
    [TOKEN_THEN]          = lit("then"),
    [TOKEN_TRUE]          = lit("true"),
    [TOKEN_UNTIL]         = lit("until"),
    [TOKEN_WHILE]         = lit("while"),

    [TOKEN_PAREN_L]       = lit("("),
    [TOKEN_PAREN_R]       = lit(")"),
    [TOKEN_BRACKET_L]     = lit("["),
    [TOKEN_BRACKET_R]     = lit("]"),
    [TOKEN_CURLY_L]       = lit("{"),
    [TOKEN_CURLY_R]       = lit("}"),

    [TOKEN_COMMA]         = lit(","),
    [TOKEN_COLON]         = lit(":"),
    [TOKEN_SEMICOLON]     = lit(";"),
    [TOKEN_ELLIPSIS_3]    = lit("..."),
    [TOKEN_ELLIPSIS_2]    = lit(".."),
    [TOKEN_PERIOD]        = lit("."),
    [TOKEN_POUND]         = lit("#"),

    [TOKEN_PLUS]          = lit("+"),
    [TOKEN_DASH]          = lit("-"),
    [TOKEN_STAR]          = lit("*"),
    [TOKEN_SLASH]         = lit("/"),
    [TOKEN_PERCENT]       = lit("%"),
    [TOKEN_CARET]         = lit("^"),

    [TOKEN_EQUAL]         = lit("="),
    [TOKEN_EQUAL_EQUAL]   = lit("=="),
    [TOKEN_TILDE_EQUAL]   = lit("~="),
    [TOKEN_ANGLE_L]       = lit("<"),
    [TOKEN_ANGLE_L_EQUAL] = lit("<="),
    [TOKEN_ANGLE_R]       = lit(">"),
    [TOKEN_ANGLE_R_EQUAL] = lit(">="),

    [TOKEN_IDENTIFIER]    = lit("<identifier>"),
    [TOKEN_STRING_LIT]    = lit("<string>"),
    [TOKEN_NUMBER_LIT]    = lit("<number>"),
    [TOKEN_ERROR]         = lit("<error>"),
    [TOKEN_EOF]           = lit("<eof>"),
};

#if defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

#undef lit

static Token_Type
check_keyword(const char *current, isize len, Token_Type type)
{
    const LString str = LULU_TOKEN_STRINGS[type];
    if (str.len == len && memcmp(str.data, current, cast(usize)len) == 0) {
        return type;
    }
    return TOKEN_IDENTIFIER;
}

static Token_Type
get_identifier_type(Lexer *lexer)
{
    const char *current = lexer->start;
    isize       len     = lexer->current - lexer->start;
    switch (current[0]) {
    case 'a': return check_keyword(current, len, TOKEN_AND);
    case 'b': return check_keyword(current, len, TOKEN_BREAK);
    case 'd': return check_keyword(current, len, TOKEN_DO);
    case 'e':
        switch (len) {
        case 3: return check_keyword(current, len, TOKEN_END);
        case 4: return check_keyword(current, len, TOKEN_ELSE);
        case 6: return check_keyword(current, len, TOKEN_ELSEIF);
        }
        break;
    case 'f':
        if (len < 3) {
            break;
        }
        switch (current[1]) {
        case 'a': return check_keyword(current, len, TOKEN_FALSE);
        case 'o': return check_keyword(current, len, TOKEN_FOR);
        case 'u': return check_keyword(current, len, TOKEN_FUNCTION);
        }
        break;
    case 'i':
        if (len != 2) {
            break;
        }
        switch (current[1]) {
        case 'f': return check_keyword(current, len, TOKEN_IF);
        case 'i': return check_keyword(current, len, TOKEN_IN);
        }
        break;
    case 'l': return check_keyword(current, len, TOKEN_LOCAL);
    case 'n':
        if (len != 3) {
            break;
        }
        switch (current[1]) {
        case 'i': return check_keyword(current, len, TOKEN_NIL);
        case 'o': return check_keyword(current, len, TOKEN_NOT);
        }
        break;
    case 'o': return check_keyword(current, len, TOKEN_OR);
    case 'p': return check_keyword(current, len, TOKEN_PRINT);
    case 'r':
        if (len < 3) {
            break;
        }
        switch (current[2]) {
        case 'p': return check_keyword(current, len, TOKEN_REPEAT);
        case 't': return check_keyword(current, len, TOKEN_RETURN);
        }
    case 't':
        if (len != 4) {
            break;
        }
        switch (current[1]) {
        case 'h': return check_keyword(current, len, TOKEN_THEN);
        case 'r': return check_keyword(current, len, TOKEN_TRUE);
        }
        break;
    case 'u': return check_keyword(current, len, TOKEN_UNTIL);
    case 'w': return check_keyword(current, len, TOKEN_WHILE);
    }
    return TOKEN_IDENTIFIER;
}

static Token
consume_identifier(Lexer *lexer)
{
    while (is_alnum(peek_char(lexer)) || peek_char(lexer) == '_') {
        advance_char(lexer);
    }
    return make_token(lexer, get_identifier_type(lexer));
}

static void
consume_base10(Lexer *lexer)
{
    while (is_digit(peek_char(lexer))) {
        advance_char(lexer);
    }
}

static void
consume_base16(Lexer *lexer)
{
    while (is_hexdigit(peek_char(lexer))) {
        advance_char(lexer);
    }
}

static Token
consume_number(Lexer *lexer)
{
    if (match_char(lexer, '0') && match_char_any(lexer, "xX")) {
        consume_base16(lexer);
        goto trailing_characters;
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
    while (is_alnum(peek_char(lexer)) || peek_char(lexer) == '_') {
        advance_char(lexer);
    }

    Token  token  = make_token(lexer, TOKEN_NUMBER_LIT);
    char  *end_ptr;
    Number number = strtod(token.start, &end_ptr);
    // We failed to convert the entire lexeme?
    if (end_ptr != (token.start + token.len)) {
        error_token(lexer, "Malformed number");
    }
    lexer->number = number;
    return token;
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

static Token
consume_string(Lexer *lexer, char quote)
{
    Builder *builder = &lexer->vm->builder;
    builder_reset(builder);
    for (;;) {
        char ch = peek_char(lexer);
        if (ch == quote || is_at_end(lexer)) {
            break;
        }

        if (ch == '\n') {
            goto unterminated_string;
        }

        if (ch == '\\') {
            // @todo 2025-01-03: Allow for escaped hex literals? e.g. "\x27"
            int esc = get_escape_char(peek_next_char(lexer));
            if (esc == -1) {
                error_token(lexer, "Invalid escape character");
            }
            ch = cast(char)esc;
            advance_char(lexer);
        }
        builder_write_char(builder, ch);
        advance_char(lexer);
    }

    if (is_at_end(lexer)) unterminated_string: {
        error_token(lexer, "Unterminated string");
    }

    // Consume closing quote.
    advance_char(lexer);

    isize       len;
    const char *data = builder_to_string(builder, &len);
    lexer->string = ostring_new(lexer->vm, data, len);
    return make_token(lexer, TOKEN_STRING_LIT);
}

Token
lexer_scan_token(Lexer *self)
{
    skip_whitespace(self);
    self->start = self->current;
    if (is_at_end(self)) {
        return make_token(self, TOKEN_EOF);
    }

    char ch = advance_char(self);
    if (is_digit(ch)) {
        return consume_number(self);
    }
    if (is_alpha(ch)) {
        return consume_identifier(self);
    }

    switch (ch) {
    case '(': return make_token(self, TOKEN_PAREN_L);
    case ')': return make_token(self, TOKEN_PAREN_R);
    case '{': return make_token(self, TOKEN_CURLY_L);
    case '}': return make_token(self, TOKEN_CURLY_R);
    case '[': {
        int opening = get_nesting(self);
        if (match_char(self, '[')) {
            skip_multiline(self, opening);
            Token token = make_token(self, TOKEN_STRING_LIT);
            int   skip  = opening + 2; // Both [ or ] with 1+ =

            token.start += skip;       // Skip opening
            token.len   -= (skip * 2); // Discard BOTH ends from view length
            self->string = ostring_new(self->vm, token.start, token.len);
            return token;
        }
        return make_token(self, TOKEN_BRACKET_L);
    }
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

    case '#': return make_token(self, TOKEN_POUND);
    case '+': return make_token(self, TOKEN_PLUS);
    case '-': return make_token(self, TOKEN_DASH);
    case '*': return make_token(self, TOKEN_STAR);
    case '/': return make_token(self, TOKEN_SLASH);
    case '%': return make_token(self, TOKEN_PERCENT);
    case '^': return make_token(self, TOKEN_CARET);

    case '~':
        if (match_char(self, '=')) {
            return make_token(self, TOKEN_TILDE_EQUAL);
        }
        error_token(self, "Expected '=' after '~'");
        break;
    case '=': return make_token(self, match_char(self, '=') ? TOKEN_EQUAL_EQUAL   : TOKEN_EQUAL);
    case '<': return make_token(self, match_char(self, '=') ? TOKEN_ANGLE_L_EQUAL : TOKEN_ANGLE_L);
    case '>': return make_token(self, match_char(self, '=') ? TOKEN_ANGLE_R_EQUAL : TOKEN_ANGLE_R);

    case '\'':
    case '\"': return consume_string(self, ch);
    }
    error_token(self, "Unexpected character");
}

void
lexer_unscan_token(Lexer *self, const Token *token)
{
    self->start   = token->start - token->len;
    self->current = token->start;
    self->line    = token->line;
}
