#include "compiler.h"
#include "vm.h"

void init_compiler(Compiler *self, Lexer *lexer, VM *vm) {
    self->lexer = lexer;
    self->vm    = vm;
}

void compile(Compiler *self, const char *input) {
    Lexer *lexer = self->lexer;
    VM *vm       = self->vm;
    init_lexer(lexer, input, vm->name);

    int line = -1;
    for (;;) {
        Token token = scan_token(lexer);
        if (token.line != line) {
            printf("%4i ", token.line);
            line = token.line;
        } else {
            printf("   | ");
        }
        printf("%2i '%.*s'\n", cast(int, token.type), token.len, token.start);
        if (token.type == TK_EOF) {
            break;
        }
    }
}
