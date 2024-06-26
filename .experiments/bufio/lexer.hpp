#pragma once

#include "io.hpp"
#include "global.hpp"

#define TOKEN_COUNT     (cast_int(Token::Type::Eof) + 1)

struct Token {
    enum class Type {
        LParen,   RParen,    // ( )
        LSquare,  RSquare,   // [ ]
        LCurly,   RCurly,    // { }
        LAngle,   RAngle,    // < >
        LAngleEq, RAngleEq,  // <= >=
        Equal1,   Equal2,    // = ==
        Dot1,     Dot2,      // . ..
        Dot3,     Comma,     // ... ,
        Colon,    Semicolon, // : ;
        Plus,     Dash,      // + -
        Star,     Slash,     // * /
        Percent,  Caret,     // % ^

        Identifier,
        String,
        Number,
        Error,
        Eof,
    };

    using Data = String*;

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
