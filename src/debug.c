#include "debug.h"
#include "limits.h"

void disassemble_chunk(const Chunk *self) {
    printf("[CONSTANTS]: '%s'\n", self->name);
    for (int i = 0; i < self->constants.len; i++) {
        const TValue *value = &self->constants.values[i];
        printf("%04i := ", i);
        print_value(value);
        printf("\n");
    }
    printf("\n");

    printf("[BYTECODE]: '%s'\n", self->name);
    for (int offset = 0; offset < self->len; ) {
        offset = disassemble_instruction(self, offset);
    }
    printf("\n");
}

#define read_byte3(chunk, offset)                                              \
    decode_byte3((chunk)->code[offset + 1],                                    \
                 (chunk)->code[offset + 2],                                    \
                 (chunk)->code[offset + 3])

#define read_constant(chunk, index) \
    ((chunk)->constants.values[(index)])

static int constant_instruction(OpCode opcode, const Chunk *chunk, int offset) {
    int arg = read_byte3(chunk, offset);
    const TValue *value = &read_constant(chunk, arg);
    printf("%-16s Kst[%i] ; ", get_opname(opcode), arg);
    print_value(value);
    printf("\n");
    return offset + 3 + 1; // 3-byte argument, +1 to get index of next opcode
}

static int byte3_instruction(OpCode opcode, const Chunk *chunk, int offset) {
    int arg = read_byte3(chunk, offset);
    printf("%-16s Top[-%i,...,-1]\n", get_opname(opcode), arg);
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
    case OP_GETGLOBAL:
    case OP_SETGLOBAL:
        return constant_instruction(opcode, self, offset);
    case OP_POP:
    case OP_CONCAT:
        return byte3_instruction(opcode, self, offset);
    case OP_NIL:    // Prefix literals
    case OP_TRUE:
    case OP_FALSE:
    case OP_EQ:     // Binary Comparison operators
    case OP_LT:
    case OP_LE:
    case OP_ADD:    // Binary Arithmetic operators
    case OP_SUB:
    case OP_MUL:
    case OP_DIV:
    case OP_MOD:
    case OP_POW:
    case OP_UNM:    // Unary operators
    case OP_NOT:
    case OP_LEN:
    case OP_PRINT:  // Other no-argument operators
    case OP_RETURN:
        return simple_instruction(opcode, offset);
    default:
        // Should not happen
        printf("Unknown opcode '%i'.\n", opcode);
        return offset + 1;
    }
}

#undef read_byte3
