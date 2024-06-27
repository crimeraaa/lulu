#pragma once

#include "io.hpp"
#include "global.hpp"

#define TOKEN_COUNT     (cast_int(Token::Type::Eof) + 1)

struct Token {
    enum class Type {
        // Keywords: https://www.lua.org/manual/5.1/manual.html#2.1
        And, Break, Do, Else, ElseIf, End, False, For, Function, If, In, Local,
        Nil, Not, Or, Repeat, Return, Then, True, Until, While,
        LParen, RParen, LSquare, RSquare, LCurly, RCurly, 
        LAngle, RAngle, LAngleEq, RAngleEq, Equal1, Equal2, TildeEq,
        Dot1, Dot2, Dot3, Comma, Colon, Semicolon,
        Plus, Dash, Star, Slash, Percent, Caret,
        Identifier, String, Number, Error, Eof,
    };

    union Data {
        Number  number;
        String *string;
    };

    Type type;
    Data data;
    int  line;
};

struct Lexer {
    Global *global;  // parent state.
    Stream *stream;  // input stream.
    Buffer *buffer;  // buffer for tokens.
    int     line;
    char    current; // current character we just read.
};

void  init_lexer(Lexer *ls, Global *g, Stream *z, Buffer *b);
Token scan_token(Lexer *ls);

const char *name_token(Token::Type t);
const char *display_token(Token::Type t);
