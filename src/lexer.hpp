#pragma once

#include "string.hpp"

/**
 * @note 2025-06-14:
 *  -   ORDER: Keep in sync with `token_strings`!
 */
enum Token_Type {
    TOKEN_INVALID,

    // Keywords
    TOKEN_AND, TOKEN_BREAK, TOKEN_DO, TOKEN_ELSE, TOKEN_ELSEIF, TOKEN_END,
    TOKEN_FALSE, TOKEN_FOR, TOKEN_FUNCTION, TOKEN_IF, TOKEN_IN, TOKEN_LOCAL,
    TOKEN_NIL, TOKEN_NOT, TOKEN_OR, TOKEN_REPEAT, TOKEN_RETURN, TOKEN_THEN,
    TOKEN_TRUE, TOKEN_UNTIL, TOKEN_WHILE,

    TOKEN_OPEN_PAREN, TOKEN_CLOSE_PAREN, // ( )
    TOKEN_OPEN_CURLY, TOKEN_CLOSE_CURLY, // { }
    TOKEN_OPEN_BRACE, TOKEN_CLOSE_BRACE, // [ ]

    TOKEN_PLUS, TOKEN_DASH, // + -
    TOKEN_ASTERISK, TOKEN_SLASH, // * /
    TOKEN_PERCENT, TOKEN_CARET, // % ^

    TOKEN_EQ, TOKEN_NOT_EQ,             // == ~=
    TOKEN_LESS, TOKEN_LESS_EQ,          // < <=
    TOKEN_GREATER, TOKEN_GREATER_EQ,    // > >=

    TOKEN_POUND, // #
    TOKEN_DOT, TOKEN_CONCAT, TOKEN_VARARG, // . .. ...
    TOKEN_COMMA, TOKEN_COLON, TOKEN_SEMI,  // , : ;

    TOKEN_ASSIGN, // =
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_EOF,
};

struct LULU_PRIVATE Token {
    Token_Type  type;
    int         line;
    LString     lexeme;
    union {
        double   number;
        OString *ostring;
    };

    static constexpr Token
    make(Token_Type type, int line, const LString &lexeme = {}, Number number = 0)
    {
        Token t{
            /* type */              type,
            /* line */              line,
            /* lexeme */            lexeme,
            /* <unnamed>::number */ {number},
        };
        return t;
    }
};

struct LULU_PRIVATE Lexer {
    lulu_VM    *vm;
    Builder    *builder;
    LString     source, script;
    const char *start;
    const char *cursor;
    int         line;
};

static constexpr int TOKEN_COUNT = TOKEN_EOF + 1;

LULU_DATA const LString
token_strings[TOKEN_COUNT];

LULU_FUNC Lexer
lexer_make(lulu_VM *vm, const LString &source, const LString &script, Builder *b);

LULU_FUNC Token
lexer_lex(Lexer *x);
