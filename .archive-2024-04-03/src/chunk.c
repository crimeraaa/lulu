#include "chunk.h"
#include "memory.h"
#include "opcodes.h"

static void init_tarray(TArray *self) {
    self->values = NULL;
    self->len = 0;
    self->cap = 0;
}

static void free_tarray(TArray *self) {
    free_array(TValue, self->values, self->cap);
    init_tarray(self);
}

static void write_tarray(TArray *self, const TValue *value) {
    if (self->len + 1 > self->cap) {
        int oldcap = self->cap;
        self->cap = grow_capacity(oldcap);
        self->values = grow_array(TValue, self->values, oldcap, self->cap);
    }
    self->values[self->len] = *value;
    self->len++;
}

void init_chunk(Chunk *self, const char *name) {
    init_tarray(&self->constants);
    self->name  = name;
    self->code  = NULL;
    self->lines = NULL;
    self->len   = 0;
    self->cap   = 0;
}

void free_chunk(Chunk *self) {
    free_tarray(&self->constants);
    free_array(Instruction, self->code, self->cap);
    free_array(int, self->lines, self->cap);
    init_chunk(self, "(freed chunk)");
}

void write_chunk(Chunk *self, Instruction byte, int line) {
    if (self->len + 1 > self->cap) {
        int oldcap  = self->cap;
        self->cap   = grow_capacity(oldcap);
        self->code  = grow_array(Instruction, self->code, oldcap, self->cap);
        self->lines = grow_array(int, self->lines, oldcap, self->cap);
    }
    self->code[self->len]  = byte;
    self->lines[self->len] = line;
    self->len++;
}

int add_constant(Chunk *self, const TValue *value) {
    write_tarray(&self->constants, value);
    return self->constants.len - 1;
}
