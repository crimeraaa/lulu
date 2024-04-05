#include "chunk.h"
#include "memory.h"

const char *const LULU_OPNAMES[] = {
    [OP_CONSTANT] = "OP_CONSTANT",
    [OP_ADD]      = "OP_ADD",
    [OP_SUB]      = "OP_SUB",
    [OP_MUL]      = "OP_MUL",
    [OP_DIV]      = "OP_DIV",
    [OP_MOD]      = "OP_MOD",
    [OP_POW]      = "OP_POW",
    [OP_UNM]      = "OP_UNM",
    [OP_RETURN]   = "OP_RETURN",
};

static_assert(arraylen(LULU_OPNAMES) == NUM_OPCODES, "Bad opname count");

void init_chunk(Chunk *self, const char *name) {
    self->name  = name;
    init_tarray(&self->constants);
    self->code  = NULL;
    self->lines = NULL;
    self->len   = 0;
    self->cap   = 0;
}

void free_chunk(Chunk *self) {
    free_tarray(&self->constants);
    free_array(Instruction, self->code, self->len);
    free_array(int, self->lines, self->len);
    init_chunk(self, "(freed chunk)");
}

void write_chunk(Chunk *self, Instruction data, int line) {
    if (self->len + 1 > self->cap) {
        int oldcap  = self->cap;
        self->cap   = grow_capacity(oldcap);
        self->code  = grow_array(Instruction, self->code, oldcap, self->cap);
        self->lines = grow_array(int , self->lines, oldcap, self->cap);
    }
    self->code[self->len]  = data;
    self->lines[self->len] = line;
    self->len++;
}

int add_constant(Chunk *self, const TValue *value) {
    write_tarray(&self->constants, value);
    return self->constants.len - 1;
}
