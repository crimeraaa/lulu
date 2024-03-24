#include "chunk.h"
#include "memory.h"
#include "opcodes.h"

void init_chunk(Chunk *self) {
    self->code = NULL;
    self->len = 0;
    self->cap = 0;
}

void free_chunk(Chunk *self) {
    free_array(Byte, self->code, self->cap);
    init_chunk(self);
}

void write_chunk(Chunk *self, Byte byte) {
    if (self->len + 1 > self->cap) {
        int oldcap = self->cap;
        self->cap  = grow_capacity(oldcap);
        self->code = grow_array(Byte, self->code, oldcap, self->cap);
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

static int simple_instruction(const char *opname, int offset) {
    printf("%s\n", opname);
    return LULU_OPCODE_NEXT(offset, LULU_OPSIZE_BYTE);
}

int disassemble_instruction(Chunk *self, int offset) {
    printf("%04i ", offset);
    Byte instruction = self->code[offset];
    switch (instruction) {
    case OP_RETURN:
        return simple_instruction("OP_RETURN", offset);
    default:
        printf("Unknown opcode %i\n", instruction);
        // Go to very next byte, whatever it may be. Might cascade errors.
        return LULU_OPCODE_NEXT(offset, LULU_OPSIZE_BYTE);
    }
}
