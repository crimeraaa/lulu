#include "debug.h"

void disassemble_chunk(const Chunk *self) {
    printf("==== %s ====\n", self->name);
    for (int offset = 0; offset < self->len;) {
        offset = disassemble_instruction(self, offset);
    }
}

static int constant_instruction(OpCode opcode, const Chunk *chunk, int offset) {
    int index = chunk->code[offset + 1];
    printf("%-16s %4i '", get_opname(opcode), index);
    print_value(&chunk->constants.values[index]);
    printf("'\n");
    return offset + 2; // 1 byte operand so move to 1 past next instruction.
}

static int simple_instruction(OpCode opcode, int offset) {
    printf("%s\n", get_opname(opcode));
    return offset + 1; // No operands so just move to next instruction.
}

int disassemble_instruction(const Chunk *self, int offset) {
    printf("%04i ", offset);
    int line = self->lines[offset];
    if (offset > 0 && line == self->lines[offset - 1]) {
        printf("    | ");
    } else {
        printf("%4i ", line);
    }
    Byte opcode = self->code[offset];
    switch (opcode) {
    case OP_CONSTANT:
        return constant_instruction(opcode, self, offset);
    case OP_RETURN:
        return simple_instruction(opcode, offset);
    default:
        printf("Unknown opcode '%i'.\n", cast(int, opcode));
        return offset + 1; // Move to immediate next instruction.
    }
}
