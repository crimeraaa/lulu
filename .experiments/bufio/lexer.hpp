#pragma once

#include "io.hpp"
#include "global.hpp"

struct Token {
    enum class Type {
        Identifier, String, Number, Error, Eof
    };

    using Data = String*;

    Type type;
    Data data;
    int  line;
};

struct Lexer {
    Slice   lexeme;  // may be invalidated by resizing of buffer!
    Global *global;  // parent state.
    Stream *stream;  // input stream.
    Buffer *buffer;  // buffer for tokens.
    int     line;
    char    current; // current character we just read.
};

void  init_lexer(Lexer *ls, Global *g, Stream *z, Buffer *b);
Token scan_token(Lexer *ls);
