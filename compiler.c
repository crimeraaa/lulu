#include "compiler.h"
#include "tokens.h"

void init_compiler(LuaCompiler *self) {}

void compile_bytecode(LuaCompiler *self, const char *source) {
    init_lexer(&self->lexer, source); 
    int line = -1;
    for (;;) {
        LuaToken token = tokenize(&self->lexer);
        if (token.line != line) {
            printf("%4i ", token.line);
        } else {
            printf("   | ");
        }
        printf("%2i '%.*s'\n", token.type, token.length, token.start);
        if (token.type == TOKEN_EOF) {
            break;
        }
    }
}
