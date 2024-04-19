#include "compiler.h"
#include "parser.h"
#include "vm.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

void init_compiler(Compiler *self, Lexer *lexer, VM *vm) {
    self->lexer = lexer;
    self->vm    = vm;
    self->chunk = NULL;
    self->localcount = 0;
    self->scopedepth = 0;
}

static Chunk *current_chunk(Compiler *self) {
    return self->chunk;
}

void emit_byte(Compiler *self, Byte data) {
    Lexer *lexer = self->lexer;
    Token *token = &lexer->consumed;
    write_chunk(current_chunk(self), data, token->line, &self->vm->allocator);
}

void emit_bytes(Compiler *self, Byte data1, Byte data2) {
    emit_byte(self, data1);
    emit_byte(self, data2);
}

void emit_byte2(Compiler *self, OpCode opcode, Byte2 data) {
    emit_byte(self, opcode);
    emit_byte(self, encode_byte2_msb(data));
    emit_byte(self, encode_byte2_lsb(data));
}

void emit_byte3(Compiler *self, OpCode opcode, Byte3 data) {
    emit_byte(self, opcode);
    emit_byte(self, encode_byte3_msb(data));
    emit_byte(self, encode_byte3_mid(data));
    emit_byte(self, encode_byte3_lsb(data));
}

void emit_return(Compiler *self) {
    emit_byte(self, OP_RETURN);
}

int make_constant(Compiler *self, const TValue *value) {
    Lexer *lexer = self->lexer;
    int index = add_constant(current_chunk(self), value, &self->vm->allocator);
    if (index >= MAX_CONSTANTS) {
        lexerror_at_consumed(lexer, "Too many constants in current chunk");
    }
    return index;
}

void emit_constant(Compiler *self, const TValue *value) {
    int index = make_constant(self, value);
    emit_byte3(self, OP_CONSTANT, cast(Byte3, index));
}

int identifier_constant(Compiler *self, const Token *name) {
    TString *ident = copy_string(self->vm, name->start, name->len);
    TValue wrapper = make_string(ident);
    return make_constant(self, &wrapper);
}

void end_compiler(Compiler *self) {
    emit_return(self);
#ifdef DEBUG_PRINT_CODE
    disassemble_chunk(current_chunk(self));
#endif
}

void begin_scope(Compiler *self) {
    self->scopedepth++;
}

void end_scope(Compiler *self) {
    self->scopedepth--;

    int popped = 0;
    while (self->localcount > 0) {
        if (self->locals[self->localcount - 1].depth <= self->scopedepth) {
            break;
        }
        popped++;
        self->localcount--;
    }
    // Don't waste 2 bytes if nothing to pop
    if (popped > 0) {
        emit_bytes(self, OP_POP, popped);
    }
}

void compile(Compiler *self, const char *input, Chunk *chunk) {
    Lexer *lexer = self->lexer;
    self->chunk  = chunk;
    init_lexer(lexer, input, self->vm);
    next_token(lexer);

    begin_scope(self); // Script/REPL can pop its own top-level locals
    while (!match_token(lexer, TK_EOF)) {
        declaration(self);
    }
    consume_token(lexer, TK_EOF, "Expected end of expression");
    end_scope(self);
    end_compiler(self);
}
