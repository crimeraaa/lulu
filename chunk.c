#include "chunk.h"
#include "memory.h"

void init_chunk(LuaChunk *self) {
    self->code = NULL;
    self->count = 0;
    self->capacity = 0;
}

void deinit_chunk(LuaChunk *self) {
    deallocate_array(uint8_t, self->code, self->capacity);
    init_chunk(self);
}

void write_chunk(LuaChunk *self, uint8_t byte) {
    if (self->count + 1 > self->capacity) {
        int oldcapacity = self->capacity;
        self->capacity = grow_capacity(oldcapacity);
        self->code = grow_array(LuaChunk, self->code, oldcapacity, self->capacity);
    }
    self->code[self->count] = byte;
    self->count++;
}

void disassemble_chunk(LuaChunk *self, const char *name) {
    printf("== %s ==\n", name);
    // We rely on `disassemble_instruction()` for iteration.
    for (int offset = 0; offset < self->count;) {
        offset = disassemble_instruction(self, offset);
    }
}

/* Simple instruction only take 1 byte for themselves. */
static int simple_instruction(const char *name, int offset) {
    printf("%s\n", name);
    return offset + 1;
}

int disassemble_instruction(LuaChunk *self, int offset) {
    printf("%04i ", offset); // Print number left-padded with 0's

    uint8_t instruction = self->code[offset];
    switch(instruction) {
    case OP_RETURN: return simple_instruction("OP_RETURN", offset);
    default:
        break;
    }
    printf("Unknown opcode %i.\n", instruction);
    return offset + 1;
}
