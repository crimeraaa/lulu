#ifndef LUA_LEXER_H
#define LUA_LEXER_H

#include "common.h"
#include "tokens.h"

/**
 * III:16.1.2   The scanner scans
 * 
 * The scanner, but I'll use the fancier term "lexer", goes through your monolithic
 * source code string. It carries state per lexeme.
 */
typedef struct {
    const char *start;   // Beginning of current lexeme in the source code.
    const char *current; // Current character in the source code.
    int line;            // Line number, for error reporting.
} LuaLexer;

void init_lexer(LuaLexer *self, const char *source);

/**
 * III:16.2.1   Scanning tokens
 * 
 * This is where the fun begins! Each call to this function scans a complete
 * token and gives you back said token for you to emit bytecode or determine
 * precedence of.
 * 
 * Remember that a token at this point does not have much syntactic purpose,
 * e.g. '(' could either be a function call or a grouping. We don't know yet.
 */
LuaToken tokenize(LuaLexer *self);

#endif /* LUA_LEXER_H */
