#include "chunk.h"
#include "object.h"
#include "memory.h"

const char *const LULU_OPNAMES[] = {
    [OP_CONSTANT]   = "OP_CONSTANT",
    [OP_NIL]        = "OP_NIL",
    [OP_TRUE]       = "OP_TRUE",
    [OP_FALSE]      = "OP_FALSE",
    [OP_EQ]         = "OP_EQ",
    [OP_LT]         = "OP_LT",
    [OP_LE]         = "OP_LE",
    [OP_ADD]        = "OP_ADD",
    [OP_SUB]        = "OP_SUB",
    [OP_MUL]        = "OP_MUL",
    [OP_DIV]        = "OP_DIV",
    [OP_MOD]        = "OP_MOD",
    [OP_POW]        = "OP_POW",
    [OP_CONCAT]     = "OP_CONCAT",
    [OP_NOT]        = "OP_NOT",
    [OP_UNM]        = "OP_UNM",
    [OP_RETURN]     = "OP_RETURN",
};

static_assert(arraylen(LULU_OPNAMES) == NUM_OPCODES, "Bad opcode count");

void init_chunk(Chunk *self, const char *name) {
    self->name  = name;
    init_tarray(&self->constants);
    self->code  = NULL;
    self->lines = NULL;
    self->len   = 0;
    self->cap   = 0;
}

void free_chunk(VM *vm, Chunk *self) {
    free_tarray(vm, &self->constants);
    free_array(vm, Byte, self->code, self->len);
    free_array(vm, int, self->lines, self->len);
    init_chunk(self, "(freed chunk)");
}

void write_chunk(VM *vm, Chunk *self, Byte data, int line) {
    if (self->len + 1 > self->cap) {
        int oldcap  = self->cap;
        self->cap   = grow_capacity(oldcap);
        self->code  = grow_array(vm, Byte, self->code, oldcap, self->cap);
        self->lines = grow_array(vm, int , self->lines, oldcap, self->cap);
    }
    self->code[self->len]  = data;
    self->lines[self->len] = line;
    self->len++;
}

int add_constant(VM *vm, Chunk *self, const TValue *value) {
    TArray *constants = &self->constants;
    // TODO: Literally anything is faster than a linear search
    for (int i = 0; i < constants->len; i++) {
        if (values_equal(&constants->values[i], value)) {
            return i;
        }
    }
    write_tarray(vm, constants, value);
    return constants->len - 1;
}
