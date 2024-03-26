#include "chunk.h"
#include "memory.h"
#include "opcodes.h"

void init_tarray(TArray *self) {
    self->values = NULL;
    self->len = 0;
    self->cap = 0;
}

void free_tarray(TArray *self) {
    free_array(TValue, self->values, self->cap);
    init_tarray(self);
}

void write_tarray(TArray *self, const TValue *value) {
    if (self->len + 1 > self->cap) {
        int oldcap = self->cap;
        self->cap = grow_capacity(oldcap);
        self->values = grow_array(TValue, self->values, oldcap, self->cap);
    }
    self->values[self->len] = *value;
    self->len++;
}

void init_chunk(Chunk *self) {
    init_tarray(&self->constants);
    self->code = NULL;
    self->lines = NULL;
    self->len = 0;
    self->cap = 0;
}

void free_chunk(Chunk *self) {
    free_tarray(&self->constants);
    free_array(Instruction, self->code, self->cap);
    free_array(int, self->lines, self->cap);
    init_chunk(self);
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

/* This style of section disassembly was taken from ChunkSpy. */
static void disassemble_section(const TArray *self, const char *name) {
    for (int i = 0; i < self->len; i++) {
        printf("%-8s ", name);
        print_value(&self->values[i]);
        printf(" ; Kst(%i)\n", i);
    } 
}

void disassemble_chunk(Chunk *self, const char *name) {
    printf("== %s ==\n", name);
    disassemble_section(&self->constants, ".const");
    for (int offset = 0; offset < self->len;) {
        offset = disassemble_instruction(self, offset);
    }
}

static void simple_instruction(OpCode opcode) {
    printf("%s\n", luaP_opnames[opcode]);
}

static void constant_instruction(OpCode opcode, const Chunk *chunk, Instruction instruction) {
    int ra = GET_REGISTER_A(instruction);  // R(A) = destination register
    int bx = GET_REGISTER_Bx(instruction); // Bx = constants index
    const char *typename = luaP_opnames[opcode];
    const TValue *value = &chunk->constants.values[bx];

    printf("%-16s R(A = %i) := Kst(Bx = %i) => '", typename, ra, bx);
    print_value(value);
    printf("' (%s)\n", luaT_typenames[value->tag]);
}

int disassemble_instruction(Chunk *self, int offset) {
    printf("[%i] ", offset);
    // Only print line number when it doesn't match previous line.
    if (offset > 0 && self->lines[offset] == self->lines[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4i ", self->lines[offset]);
    }
    Instruction instruction = self->code[offset];
    OpCode opcode = GET_OPCODE(instruction);
    switch (opcode) {
    case OP_CONSTANT:
        constant_instruction(OP_CONSTANT, self, instruction);
        break;
    case OP_RETURN:
        simple_instruction(OP_RETURN);
        break;
    default:
        printf("Unknown opcode %i.\n", opcode);
        break;
    }
    // We can assume that instructions always come one after the other.
    return offset + 1;
}
