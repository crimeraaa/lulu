#include "debug.h"
#include "limits.h"

void disassemble_constants(const Chunk *self) {
    const TArray *array = &self->constants;
    printf("[CONSTANTS]:\n");
    for (int i = 0; i < array->len; i++) {
        printf("[%i] := ", i);
        print_value(&array->values[i]);
        printf("\n");
    }
    printf("\n");
}

void disassemble_chunk(const Chunk *self) {
    disassemble_constants(self);
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

static void constant_instruction(OpCode op, const Chunk *chunk, int offset) {
    int arg = read_byte3(chunk, offset);
    printf("%-16s Kst[%i] ; ", get_opname(op), arg);
    print_value(&read_constant(chunk, arg));
    printf("\n");
}

// Assumes all instructions that manipulate ranges of values on the stack will
// only ever have a 1-byte argument.
static void range_instruction(OpCode op, const Chunk *chunk, int offset) {
    int arg = read_byte(chunk, offset);
    printf("%-16s Top[%i...-1]\n", get_opname(op), -arg);
}

static void local_instruction(OpCode op, const Chunk *chunk, int offset) {
    int arg = read_byte(chunk, offset);
    printf("%-16s Loc[%i]\n", get_opname(op), arg);
}

int disassemble_instruction(const Chunk *self, int offset) {
    printf("%04x ", offset);
    int line = self->lines[offset];
    if (offset > 0 && line == self->lines[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4i ", line);
    }
    OpCode op = self->code[offset];
    switch (op) {
    case OP_CONSTANT:
    case OP_GETGLOBAL:
    case OP_SETGLOBAL:
        constant_instruction(op, self, offset);
        break;
    case OP_GETLOCAL:
    case OP_SETLOCAL:
        local_instruction(op, self, offset);
        break;
    case OP_POP:
    case OP_NIL:
    case OP_CONCAT:
    case OP_PRINT:
        range_instruction(op, self, offset);
        break;
    case OP_GETTABLE:
    case OP_SETTABLE:
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
    case OP_RETURN:
        printf("%s\n", get_opname(op));
        break;
    default:
        // Should not happen
        printf("Unknown opcode '%i'.\n", op);
        return offset + 1;
    }
    return offset + get_opargc(op) + 1;
}

#undef read_byte
#undef read_byte3
#undef read_byte3_if
#undef read_constant
