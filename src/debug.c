#include "debug.h"
#include "limits.h"

void disassemble_chunk(const Chunk *self) {
    printf("[CONSTANTS]: '%s'\n", self->name);
    for (int i = 0; i < self->constants.len; i++) {
        const TValue *value = &self->constants.values[i];
        printf("%04i := '", i);
        print_value(value);
        printf("' (%s)\n", get_typename(value));
    }
    printf("\n");

    printf("[BYTECODE]: '%s'\n", self->name);
    for (int offset = 0; offset < self->len; offset++) {
        disassemble_instruction(self, offset);
    }
    printf("\n");
}

// 1 Argument: Bx (unsigned 26-bit integer)
static void constant_instruction(const Instruction self, const Chunk *chunk) {
    OpCode opcode = getarg_op(self);
    int index     = getarg_Bx(self);
    const TValue *value = &chunk->constants.values[index];
    printf("%-16s Constants[Bx := %i] ; '", get_opname(opcode), index);
    print_value(value);
    printf("' (%s)\n", get_typename(value));
}

// No directly encoded arguments. However, they may be implicitly on the stack.
static void simple_instruction(const Instruction self) {
    OpCode opcode = getarg_op(self);
    printf("%s\n", get_opname(opcode));
}

void disassemble_instruction(const Chunk *self, int offset) {
    printf("%04i ", offset);
    int line = self->lines[offset];
    if (offset > 0 && line == self->lines[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4i ", line);
    }
    Instruction inst = self->code[offset];
    OpCode opcode = getarg_op(inst);
    switch (opcode) {
    case OP_CONSTANT:
        constant_instruction(inst, self);
        break;
    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_DIV:
    case OP_MOD:
    case OP_POW:
    case OP_UNM:
    case OP_RETURN:
        simple_instruction(inst);
        break;
    }
}
