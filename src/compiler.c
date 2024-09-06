#include "compiler.h"
#include "vm.h"

#include <stdio.h>

void lulu_Compiler_init(lulu_VM *vm, lulu_Compiler *self, lulu_Lexer *lexer)
{
    self->vm    = vm;
    self->lexer = lexer;
}

void lulu_Compiler_compile(lulu_Compiler *self, cstring input)
{
    lulu_Lexer *lexer = self->lexer;
    int         line  = -1;
    unused(self);
    lulu_Lexer_init(self->vm, lexer, input);
    for (;;) {
        lulu_Token token = lulu_Lexer_scan_token(lexer);
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
