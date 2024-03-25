#include "chunk.h"
#include "memory.h"
#include "opcodes.h"

void init_chunk(Chunk *self) {
    self->code = NULL;
    self->len = 0;
    self->cap = 0;
}

void free_chunk(Chunk *self) {
    free_array(Instruction, self->code, self->cap);
    init_chunk(self);
}

void write_chunk(Chunk *self, Instruction byte) {
    if (self->len + 1 > self->cap) {
        int oldcap = self->cap;
        self->cap  = grow_capacity(oldcap);
        self->code = grow_array(Instruction, self->code, oldcap, self->cap);
    }
    self->code[self->len] = byte;
    self->len++;
}

void disassemble_chunk(Chunk *self, const char *name) {
    printf("== %s ==\n", name);
    for (int offset = 0; offset < self->len;) {
        offset = disassemble_instruction(self, offset);
    }
}

static void simple_instruction(const char *opname) {
    printf("%s\n", opname);
}

int disassemble_instruction(Chunk *self, int offset) {
    printf("%04i ", offset);
    Instruction instruction = self->code[offset];
    switch (instruction) {
    case OP_RETURN:
        simple_instruction("OP_RETURN");
        break;
    default:
        printf("Unknown opcode %i\n", instruction);
        break;
    }
    // We can assume that instructions always come one after the other.
    return offset + 1;
}
