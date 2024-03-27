#include "debug.h"
#include "opcodes.h"

/* This style of section disassembly was taken from ChunkSpy. */
static void disassemble_section(const TArray *self, const char *name) {
    for (int i = 0; i < self->len; i++) {
        printf("%-8s ", name);
        print_value(&self->values[i]);
        printf(" ; Kst(%i)\n", i);
    }
}

void disassemble_chunk(Chunk *self, const char *name) {
    printf("BEGIN: '%s'\n", name);
    disassemble_section(&self->constants, ".const");
    for (int offset = 0; offset < self->len;) {
        offset = disassemble_instruction(self, offset);
    }
    printf("END:   '%s'\n", name);
}

static void simple_instruction(OpCode opcode) {
    printf("%s\n", get_opname(opcode));
}

static void constant_instruction(OpCode opcode, const Chunk *chunk, Instruction instruction) {
    int ra = GETARG_RA(instruction);  // R(A) = destination register
    int bx = GETARG_RBx(instruction); // Bx = constants index
    const TValue *value = &chunk->constants.values[bx];
    printf("%-16s R(A=%i) := Kst(Bx=%i) ; '", get_opname(opcode), ra, bx);
    print_value(value);
    printf("' (%s)\n", astypename(value));
}

static void negate_instruction(OpCode opcode, Instruction instruction) {
    int ra = GETARG_RA(instruction); // R(A) := destination register
    int rb = GETARG_RB(instruction); // R(B) := source register
    printf("%-16s R(A=%i) := -R(B=%i)\n", get_opname(opcode), ra, rb);
}

int disassemble_instruction(Chunk *self, int offset) {
    printf("[%04i] ", offset);
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
    case OP_UNM:
        negate_instruction(OP_UNM, instruction);
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
