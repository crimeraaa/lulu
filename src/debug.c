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
    for (int offset = 0; offset < self->len; ) {
        offset = disassemble_instruction(self, offset);
    }
    printf("\n");
}

static int constant_instruction(OpCode opcode, const Chunk *chunk, int offset) {
    int index = decode_byte3(chunk->code[offset + 1],
                             chunk->code[offset + 2],
                             chunk->code[offset + 3]);
    const TValue *value = &chunk->constants.values[index];
    printf("%-16s Constants[Bx := %i] ; '", get_opname(opcode), index);
    print_value(value);
    printf("' (%s)\n", get_typename(value));
    return offset + 3 + 1; // 3-byte argument, +1 to get index of next opcode
}

static int simple_instruction(OpCode opcode, int offset) {
    printf("%s\n", get_opname(opcode));
    return offset + 1; // 0-byte argument, +1 to get index of next opcode
}

int disassemble_instruction(const Chunk *self, int offset) {
    printf("%04i ", offset);
    int line = self->lines[offset];
    if (offset > 0 && line == self->lines[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4i ", line);
    }
    OpCode opcode = self->code[offset];
    switch (opcode) {
    case OP_CONSTANT:
        return constant_instruction(opcode, self, offset);
    case OP_NIL:
    case OP_TRUE:
    case OP_FALSE:
    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_DIV:
    case OP_MOD:
    case OP_POW:
    case OP_NOT:
    case OP_UNM:
    case OP_RETURN:
        return simple_instruction(opcode, offset);
    default:
        // Should not happen
        printf("Unknown opcode '%i'.\n", opcode);
        return offset + 1;
    }
}
