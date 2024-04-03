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

void disassemble_chunk(Chunk *self) {
    printf("disassembly: '%s'\n", self->name);
    disassemble_section(&self->constants, ".const");
    for (int offset = 0; offset < self->len;) {
        offset = disassemble_instruction(self, offset);
    }
    printf("\n");
}

static void constant_instruction(OpCode opcode, const Chunk *chunk, Instruction instruction) {
    int ra  = GETARG_A(instruction);  // R(A) = destination register
    int rbx = GETARG_Bx(instruction); // Bx = constants index
    const TValue *value = &chunk->constants.values[rbx];
    printf("%-16s %4i %4i %4c ; R(A) := Kst(Bx) '", 
        get_opname(opcode), ra, rbx, ' ');
    print_value(value);
    printf("' (%s)\n", astypename(value));
}

static void negate_instruction(Instruction instruction) {
    int ra = GETARG_A(instruction); // R(A) := destination register
    int rb = GETARG_B(instruction); // R(B) := source register
    printf("%-16s %4i %4i %4c ; R(A) := -R(B)\n", 
        get_opname(OP_UNM), ra, rb, ' ');
}

static void arith_instruction(OpCode opcode, Instruction instruction, char arith_op) { 
    int ra  = GETARG_A(instruction); // R(A) := destination register
    int rkb = GETARG_B(instruction); // RK(B) := left hand side of operation
    int rkc = GETARG_C(instruction); // RK(C) := right hand side of operation
    printf("%-16s %4i %4i %4i ; R(A) := RK(B) %c RK(C)\n", 
        get_opname(opcode), ra, rkb, rkc, arith_op);
}

static void return_instruction(Instruction instruction) {
    int ra = GETARG_A(instruction); // R(A) := first argument to return
    int rb = GETARG_B(instruction); // If 0 then return up to 'top'.
    printf("%-16s %4i %4i %4c ; return R(A), ..., R(A+B-2)\n",
        get_opname(OP_RETURN), ra, rb, ' ');
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
    case OP_ADD:
        arith_instruction(OP_ADD, instruction, '+');
        break;
    case OP_SUB:
        arith_instruction(OP_SUB, instruction, '-');
        break;
    case OP_MUL:
        arith_instruction(OP_MUL, instruction, '*');
        break;
    case OP_DIV:
        arith_instruction(OP_DIV, instruction, '/');
        break;
    case OP_MOD:
        arith_instruction(OP_MOD, instruction, '%');
        break;
    case OP_POW:
        arith_instruction(OP_POW, instruction, '^');
        break;
    case OP_UNM:
        negate_instruction(instruction);
        break;
    case OP_RETURN:
        return_instruction(instruction);
        break;
    default:
        printf("Unknown opcode %i.\n", opcode);
        break;
    }
    // We can assume that instructions always come one after the other.
    return offset + 1;
}
