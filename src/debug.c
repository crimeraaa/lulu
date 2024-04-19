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

#define read_byte(chunk, offset) \
    ((chunk)->code[(offset) + 1])

#define read_byte3(chunk, offset)                                              \
    decode_byte3((chunk)->code[(offset) + 1],                                  \
                 (chunk)->code[(offset) + 2],                                  \
                 (chunk)->code[(offset) + 3])

#define read_byte3_if(cond, chunk, offset) \
    ((cond) ? read_byte3(chunk, offset) : read_byte(chunk, offset))

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

// Opcodes with no arguments.
static int simple_instruction(OpCode opcode, int offset) {
    printf("%s\n", get_opname(opcode));
    return offset + 1;
}

static int range_instruction(OpCode opcode, const Chunk *chunk, int offset) {
    bool ismulti = (opcode == OP_CONCAT);
    int arg = read_byte3_if(ismulti, chunk, offset);
    printf("%-16s Top[%i...-1]\n", get_opname(opcode), -arg);
    return offset + (ismulti ? 3 : 1) + 1;
}

static int local_instruction(OpCode opcode, const Chunk *chunk, int offset) {
    int arg = read_byte(chunk, offset);
    printf("%-16s Loc[%i]\n", get_opname(opcode), arg);
    return offset + 1 + 1;
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
    case OP_GETLOCAL:
    case OP_SETLOCAL:
        return local_instruction(opcode, self, offset);
    case OP_POP:
    case OP_NIL:
    case OP_CONCAT:
        return range_instruction(opcode, self, offset);
    case OP_TRUE:   // Prefix literals
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

#undef read_byte
#undef read_byte3
#undef read_byte3_if
#undef read_constant
