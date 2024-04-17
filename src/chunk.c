#include "chunk.h"
#include "object.h"
#include "memory.h"

const char *const LULU_OPNAMES[] = {
    [OP_CONSTANT]   = "CONSTANT",
    [OP_NIL]        = "NIL",
    [OP_TRUE]       = "TRUE",
    [OP_FALSE]      = "FALSE",
    [OP_POP]        = "POP",
    [OP_GETGLOBAL]  = "GETGLOBAL",
    [OP_SETGLOBAL]  = "SETGLOBAL",
    [OP_EQ]         = "EQ",
    [OP_LT]         = "LT",
    [OP_LE]         = "LE",
    [OP_ADD]        = "ADD",
    [OP_SUB]        = "SUB",
    [OP_MUL]        = "MUL",
    [OP_DIV]        = "DIV",
    [OP_MOD]        = "MOD",
    [OP_POW]        = "POW",
    [OP_CONCAT]     = "CONCAT",
    [OP_UNM]        = "UNM",
    [OP_NOT]        = "NOT",
    [OP_LEN]        = "LEN",
    [OP_PRINT]      = "PRINT",
    [OP_RETURN]     = "RETURN",
};

static_assert(array_len(LULU_OPNAMES) == NUM_OPCODES, "Bad opcode count");

void init_chunk(Chunk *self, const char *name) {
    init_tarray(&self->constants);
    self->name  = name;
    self->code  = NULL;
    self->lines = NULL;
    self->len   = 0;
    self->cap   = 0;
}

void free_chunk(Chunk *self, Allocator *allocator) {
    free_tarray(&self->constants, allocator);
    free_array(Byte, self->code,  self->len, allocator);
    free_array(int,  self->lines, self->len, allocator);
    init_chunk(self, "(freed chunk)");
}

void write_chunk(Chunk *self, Byte data, int line, Allocator *allocator) {
    if (self->len + 1 > self->cap) {
        int prev    = self->cap;
        int next    = grow_capacity(prev);
        self->code  = resize_array(Byte, self->code,  prev, next, allocator);
        self->lines = resize_array(int,  self->lines, prev, next, allocator);
        self->cap   = next;
    }
    self->code[self->len]  = data;
    self->lines[self->len] = line;
    self->len++;
}

int add_constant(Chunk *self, const TValue *value, Allocator *allocator) {
    TArray *constants = &self->constants;
    // TODO: Literally anything is faster than a linear search
    for (int i = 0; i < constants->len; i++) {
        if (values_equal(&constants->values[i], value)) {
            return i;
        }
    }
    write_tarray(constants, value, allocator);
    return constants->len - 1;
}
