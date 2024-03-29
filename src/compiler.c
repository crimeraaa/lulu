#include "compiler.h"

void compile(Compiler *self, const char *input) {
    LexState *lexstate = self->lexstate;
    init_lexstate(lexstate, input);
    int line = -1;
    for (;;) {
        Token token = scan_token(lexstate);
        if (token.line != line) {
            printf("%4i ", token.line);
            line = token.line;
        } else {
            printf("   | ");
        }
        printf("%2i '%.*s'\n", token.type, token.len, token.start);
        if (token.type == TK_EOF) {
            break;
        }
    }
}
