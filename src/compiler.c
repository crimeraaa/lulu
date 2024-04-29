#include "compiler.h"
#include "parser.h"
#include "vm.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

void init_compiler(Compiler *self, Lexer *lexer, VM *vm) {
    self->lexer      = lexer;
    self->vm         = vm;
    self->chunk      = NULL;
    self->localcount = 0;
    self->scopedepth = 0;
    self->stacktotal = 0;
    self->stackusage = 0;
}

static void adjust_stackinfo(Compiler *self, int push, int pop) {
    Lexer *lexer = self->lexer;
    self->stackusage += push - pop;
    if (self->stackusage > MAX_STACK) {
        lexerror_at_consumed(lexer, "Function uses too many stack slots");
    }
    if (self->stackusage > self->stacktotal) {
        self->stacktotal = self->stackusage;
    }
}

static void emit_byte(Compiler *self, Byte data) {
    Alloc *alloc = &self->vm->alloc;
    Lexer *lexer = self->lexer;
    Token *token = &lexer->consumed;
    write_chunk(current_chunk(self), data, token->line, alloc);
}

Chunk *current_chunk(Compiler *self) {
    return self->chunk;
}

void emit_opcode(Compiler *self, OpCode op) {
    OpInfo *info = get_opinfo(op);
    emit_byte(self, op);
    adjust_stackinfo(self, info->push, info->pop);
}

void emit_oparg1(Compiler *self, OpCode op, Byte arg) {
    OpInfo *info = get_opinfo(op);
    emit_byte(self, op);
    emit_byte(self, arg);

    if (info->push == VAR_DELTA) {
        adjust_stackinfo(self, cast(int, arg), info->pop);
    } else if (info->pop == VAR_DELTA) {
        adjust_stackinfo(self, info->push, cast(int, arg));
    } else {
        adjust_stackinfo(self, info->push, info->pop);
    }
}

void emit_oparg2(Compiler *self, OpCode op, Byte2 arg) {
    emit_byte(self, op);
    emit_byte(self, encode_byte2_msb(arg));
    emit_byte(self, encode_byte2_lsb(arg));
}

void emit_oparg3(Compiler *self, OpCode op, Byte3 arg) {
    // We assume that no opcode with a 3-byte argument has a variable delta.
    emit_opcode(self, op);
    emit_byte(self, encode_byte3_msb(arg));
    emit_byte(self, encode_byte3_mid(arg));
    emit_byte(self, encode_byte3_lsb(arg));
}

void emit_return(Compiler *self) {
    emit_opcode(self, OP_RETURN);
}

int make_constant(Compiler *self, const TValue *value) {
    Lexer *lexer = self->lexer;
    int index = add_constant(current_chunk(self), value, &self->vm->alloc);
    if (index + 1 > MAX_CONSTS) {
        lexerror_at_consumed(lexer, "Too many constants in current chunk");
    }
    return index;
}

void emit_constant(Compiler *self, const TValue *value) {
    int index = make_constant(self, value);
    emit_oparg3(self, OP_CONSTANT, index);
}

int identifier_constant(Compiler *self, const Token *name) {
    TString *ident = copy_string(self->vm, name->start, name->len);
    TValue wrapper = make_string(ident);
    return make_constant(self, &wrapper);
}

void end_compiler(Compiler *self) {
    emit_return(self);
#ifdef DEBUG_PRINT_CODE
    printf("[STACK USAGE]:\n"
           "NET:    %i\n"
           "MOST:   %i\n",
           self->stackusage,
           self->stacktotal);
    printf("\n");
    disassemble_chunk(current_chunk(self));
#endif
}

void begin_scope(Compiler *self) {
    Lexer *lexer = self->lexer;
    if ((self->scopedepth += 1) > MAX_LEVELS) {
        lexerror_at_token(lexer, "Function uses too many syntax levels");
    }
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
        emit_oparg1(self, OP_POP, popped);
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
