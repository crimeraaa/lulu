#include <ctype.h>
#include "lex.h"
#include "compiler.h"
#include "vm.h"

#define isidentstart(ch)    (isalpha(ch) || (ch) == '_')
#define isident(ch)         (isalnum(ch) || (ch) == '_')

// --- LEXER -------------------------------------------------------------- {{{1

void init_lex(Lexer *self, Compiler *compiler, const char *name, const char *input) {
    self->token      = compoundlit(Token, 0);
    self->lookahead  = compoundlit(Token, 0);
    self->func       = compiler;
    self->name       = name;
    self->lexeme     = input;
    self->position   = input;
    self->linenumber = 1;
    self->lastline   = 1;
}

// --- BASIC LEXER MANIPULATION ------------------------------------------- {{{2

// Since we use one giant nul-terminated string we assume nul means we're done.
// Test sending a null char by typing `CTRL + @` where `@ := SHIFT + 2`.
static bool is_at_end(const Lexer *self) {
    return *self->position == '\0';
}

// Return the current character being pointed to and increment the position.
static char next_char(Lexer *self) {
    return *(self->position++);
}

// Get the current character being pointed at without modifying any state.
static char peek_current_char(const Lexer *self) {
    return *self->position;
}

// Get the character immediately right after where our position pointer is.
static char peek_next_char(const Lexer *self) {
    if (is_at_end(self)) {
        return '\0';
    }
    return *(self->position + 1);
}

// Return `true` and advance the position pointer if matches, else do nothing.
static bool match_char(Lexer *self, char expected) {
    if (is_at_end(self)) {
        return false;
    }
    if (*self->position != expected) {
        return false;
    }
    self->position++;
    return true;
}

static Token make_token(const Lexer *self, TkType type) {
    Token token;
    token.type  = type;
    token.start = self->lexeme;
    token.len   = cast(int, self->position - self->lexeme);
    token.line  = self->linenumber;
    return token;
}

// Please only pass C string literals, as these are usually stored in the
// executable's read-only data section and thus their validity is guaranteed.
static Token error_token(const Lexer *self, const char *info) {
    Token token;
    token.type  = TK_ERROR;
    token.start = info;
    token.len   = cast(int, strlen(info));
    token.line  = self->linenumber;
    return token;
}

// 2}}} ------------------------------------------------------------------------

// --- LEXER: IGNOREABLE TOKENS ------------------------------------------- {{{2

static void skip_simple_comment(Lexer *self) {
    while (peek_current_char(self) != '\n' && !is_at_end(self)) {
        next_char(self);
    }
}

static void skip_multiline_comment(Lexer *self, int nesting) {
    unused(nesting);
    for (;;) {
        char lhs = next_char(self);
        char rhs = peek_current_char(self);
        // TODO: Using `nesting`, check for the appropriate closing pair
        if (lhs == ']' && rhs == ']') {
            next_char(self); // We can safely consume the 2nd ']'.
            break;
        }
        // Will call `longjmp` to get us out of here.
        // Somewhat hacky to modify the lookahead token directly but this works.
        if (lhs == '\0' || rhs == '\0') {
            self->lookahead.line = self->linenumber;
            lexerror_lookahead(self, "Unfinished long comment");
            break;
        }
        // `lhs` was 'consumed' for lack of better word.
        if (lhs == '\n') {
            self->linenumber++;
        }
    }
}

// Assumes we are pointing to the first character after a '--' token.
// If we have a '[' right after the '--', we still need to determine if it's a
// single-line comment or a multi-line comment.
static void skip_comment(Lexer *self) {
    if (match_char(self, '[')) {
        // Determine how many nested '[]' pairs are allowed, using the '='
        // syntax, e.g. `--[==[]==]` allows you to nest 2 [] pairs.
        int nesting = 0;
        while (match_char(self, '=')) {
            nesting++;
        }
        // If we don't find another '[' we can assume this is a simple comment.
        if (!match_char(self, '[')) {
            skip_simple_comment(self);
        } else {
            skip_multiline_comment(self, nesting);
        }
    } else {
        skip_simple_comment(self);
    }
}

static void skip_whitespace(Lexer *self) {
    for (;;) {
        char ch = peek_current_char(self);
        switch (ch) {
        case ' ':
        case '\r':
        case '\t':
            next_char(self);
            break;
        case '\n':
            self->linenumber++;
            next_char(self);
            break;
        case '-':
            // Comment aren't whitespace but we may as well do it here.
            if (peek_next_char(self) == '-') {
                // Consume the first '-' and second '-' so we can point at the
                // first character that is directly after them.
                next_char(self);
                next_char(self);
                skip_comment(self);
                break;
            } else {
                return;
            }
        default:
            return;
        }
    }
}

// 2}}} ------------------------------------------------------------------------

// --- LEXER: KEYWORD HELPERS --------------------------------------------- 2{{{

typedef struct {
    const char *data;
    int len;
} Keyword;

#define make_keyword(word)  (Keyword){word, arraylen(word) - 1}

static const Keyword LUA_KEYWORDS[NUM_RESERVED] = {
    [TK_AND]        = make_keyword("and"),
    [TK_BREAK]      = make_keyword("break"),
    [TK_DO]         = make_keyword("do"),
    [TK_ELSE]       = make_keyword("else"),
    [TK_ELSEIF]     = make_keyword("elseif"),
    [TK_END]        = make_keyword("end"),
    [TK_FALSE]      = make_keyword("false"),
    [TK_FOR]        = make_keyword("for"),
    [TK_FUNCTION]   = make_keyword("function"),
    [TK_IF]         = make_keyword("if"),
    [TK_IN]         = make_keyword("in"),
    [TK_LOCAL]      = make_keyword("local"),
    [TK_NIL]        = make_keyword("nil"),
    [TK_NOT]        = make_keyword("not"),
    [TK_OR]         = make_keyword("or"),
    [TK_RETURN]     = make_keyword("return"),
    [TK_THEN]       = make_keyword("then"),
    [TK_TRUE]       = make_keyword("true"),
    [TK_WHILE]      = make_keyword("while"),
};

#define strings_equal(s1, s2, len)    (memcmp(s1, s2, len) == 0)

/**
 * @brief   Check the `word` with given `len` against the given keyword using
 *          `expected` to look up in the `LUA_KEYWORDS` array.
 *
 * @note    We could use an offset but since we already check `word` and the
 *          keyword's length, and all the keywords are rather short, it seems
 *          like a very useless optimization in this case.
 */
static TkType check_keyword(const char *word, int len, TkType expected) {
    const Keyword *kw = &LUA_KEYWORDS[expected];
    if (len == kw->len) {
        if (strings_equal(word, kw->data, len)) {
            return expected;
        }
    }
    return TK_NAME;
}

// 2}}} ------------------------------------------------------------------------

// LEXER: VARIABLE LENGTH TOKENS ------------------------------------------ 2{{{

static TkType get_identifier_type(Lexer *self) {
    const int len    = cast(int, self->position - self->lexeme);
    const char *word = self->lexeme;

// Helper macro so I don't go insane.
#define check_keyword(T)    check_keyword(word, len, T)

    // Sort of a trie
    switch (word[0]) {
    case 'a': return check_keyword(TK_AND);
    case 'b': return check_keyword(TK_BREAK);
    case 'd': return check_keyword(TK_DO);
    case 'e':
        switch (len) {
        case arraylen("end") - 1:    return check_keyword(TK_END);
        case arraylen("else") - 1:   return check_keyword(TK_ELSE);
        case arraylen("elseif") - 1: return check_keyword(TK_ELSEIF);
        default:
            break;
        }
        break;
    case 'f':
        switch (word[1]) {
        case 'a': return check_keyword(TK_FALSE);
        case 'o': return check_keyword(TK_FOR);
        case 'u': return check_keyword(TK_FUNCTION);
        default:
            break;
        }
        break;
    case 'i':
        switch (word[1]) {
        case 'f': return check_keyword(TK_IF);
        case 'n': return check_keyword(TK_IN);
        default:
            break;
        }
        break;
    case 'l': return check_keyword(TK_LOCAL);
    case 'n':
        switch (word[1]) {
        case 'i': return check_keyword(TK_NIL);
        case 'o': return check_keyword(TK_NOT);
        default:
            break;
        }
        break;
    case 'o': return check_keyword(TK_OR);
    case 'r': return check_keyword(TK_RETURN);
    case 't':
        switch (word[1]) {
        case 'h': return check_keyword(TK_THEN);
        case 'r': return check_keyword(TK_TRUE);
        default:
            break;
        }
        break;
    case 'w': return check_keyword(TK_WHILE);
    }
    return TK_NAME;

// Only used inside of this function so get rid of it.
#undef check_keyword

}

static Token make_identifier_token(Lexer *self) {
    char ch;
    while ((ch = peek_current_char(self)) && isident(ch)) {
        next_char(self);
    }
    return make_token(self, get_identifier_type(self));
}

static Token make_number_token(Lexer *self) {
    while (isdigit(peek_current_char(self))) {
        next_char(self);
    }
    // Look for a fractional part. Lua also allows literals like `1.`.
    if (match_char(self, '.')) {
        while (isdigit(peek_current_char(self))) {
            next_char(self);
        }
    }
    return make_token(self, TK_NUMBER);
}

static Token make_string_token(Lexer *self, char quote) {
    while (peek_current_char(self) != quote && !is_at_end(self)) {
        if (peek_current_char(self) == '\n') {
            return error_token(self, "Unfinished string");
        }
        next_char(self);
    }
    if (is_at_end(self)) {
        return error_token(self, "Unfinished string");
    }
    // Consume the closing quote.
    next_char(self);
    return make_token(self, TK_STRING);
}

// 2}}} ------------------------------------------------------------------------

// Helper. If we match `ch`, evaluate to `y`. Else evaluate to `n`.
#define make_ifeq(ls, ch, y, n) \
    make_token(ls, match_char(ls, ch) ? (y) : (n))

Token scan_token(Lexer *self) {
    // Ensure the lexeme points to something that isn't a whitespace character.
    skip_whitespace(self);

    // Since each call to this function scans a complete token we know we are at
    // the start of a new token.
    self->lexeme = self->position;
    if (is_at_end(self)) {
        return make_token(self, TK_EOF);
    }

    char ch = next_char(self);
    if (isalpha(ch)) {
        return make_identifier_token(self);
    }
    if (isdigit(ch)) {
        return make_number_token(self);
    }

    switch (ch) {
    // Arithmetic operators
    case '+': return make_token(self, TK_PLUS);
    case '-': return make_token(self, TK_DASH);
    case '*': return make_token(self, TK_STAR);
    case '/': return make_token(self, TK_SLASH);
    case '%': return make_token(self, TK_PERCENT);
    case '^': return make_token(self, TK_CARET);

    // Relational operators
    case '~':
        if (match_char(self, '=')) {
            make_token(self, TK_NEQ);
        } else {
            return error_token(self, "Expected '=' after '~'");
        }
    case '=': return make_ifeq(self, '=', TK_ASSIGN, TK_EQ);
    case '>': return make_ifeq(self, '=', TK_GE, TK_GT);
    case '<': return make_ifeq(self, '=', TK_LE, TK_LT);

    // Balanced pairs
    case '(': return make_token(self, TK_LPAREN);
    case ')': return make_token(self, TK_RPAREN);
    case '[': return make_token(self, TK_LBRACKET);
    case ']': return make_token(self, TK_RBRACKET);
    case '{': return make_token(self, TK_LCURLY);
    case '}': return make_token(self, TK_RCURLY);

    // Punctuation marks
    case ',': return make_token(self, TK_COMMA);
    case ';': return make_token(self, TK_SEMICOL);
    case '.':
        // If the next character also a '.'? If yes, we definitely have at least
        // a '..' token. If we match it again we have a '...' token.
        if (match_char(self, '.')) {
            return make_ifeq(self, '.', TK_VARARG, TK_CONCAT);
        } else {
            return make_token(self, TK_PERIOD);
        }
    case '"':  return make_string_token(self, '"');
    case '\'': return make_string_token(self, '\'');
    }

    return error_token(self, "Unexpected symbol");
}

// 1}}} ------------------------------------------------------------------------

// --- TOKENIZER ---------------------------------------------------------- 1{{{

void lexerror_at(Lexer *self, const Token *token, const char *info) {
    lua_VM *vm   = self->func->vm;
    Chunk *chunk = vm->chunk; // Later on, VM will have a CallInfo array.
    fprintf(stderr, "%s:%i: %s ", chunk->name, token->line, info);
    if (token->type == TK_EOF) {
        fprintf(stderr, "at end\n");
    } else if (token->type == TK_ERROR) {
        // Do nothing since error tokens already passed the necessary message
        // and an error token is not located within the source code.
    } else {
        fprintf(stderr, "near '%.*s'\n", token->len, token->start);
    }
    longjmp(vm->errorjmp, 1); // Set in `vm.c:compile()`.
}

void lexerror_consumed(Lexer *self, const char *info) {
    lexerror_at(self, &self->token, info);
}

void lexerror_lookahead(Lexer *self, const char *info) {
    lexerror_at(self, &self->lookahead, info);
}

void next_token(Lexer *self) {
    self->lastline  = self->linenumber;
    self->token     = self->lookahead;
    self->lookahead = scan_token(self);
    // Error tokens already have error messages so we can just use that.
    // NOTE: This will call `longjmp`.
    if (self->lookahead.type == TK_ERROR) {
        lexerror_lookahead(self, self->token.start);
    }
}

void consume_token(Lexer *self, TkType expected, const char *info) {
    if (self->lookahead.type == expected) {
        next_token(self);
    } else {
        lexerror_lookahead(self, info);
    }
}

// 1}}} ------------------------------------------------------------------------
