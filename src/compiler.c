#include "compiler.h"

#include <stdio.h>

void lulu_Compiler_compile(lulu_Compiler *self, lulu_Lexer *lex, cstring input)
{
    int line = -1;
    unused(self);
    lulu_Lexer_init(lex, input);
    for (;;) {
        lulu_Token token = lulu_Lexer_scan_token(lex);
        if (token.line != line) {
            printf("%4i ", token.line);
            line = token.line;
        } else {
            printf("   | ");
        }
        printf("%2i '%.*s'\n", token.type, cast(int)token.string.len, token.string.data);
        if (token.type == TOKEN_EOF) {
            break;
        }
    }
}
