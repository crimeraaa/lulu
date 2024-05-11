#include "debug.h"
#include "limits.h"

static void disassemble_constants(const Chunk *self)
{
    const VArray *array = &self->constants;
    printf("[CONSTANTS]:\n");
    for (int i = 0; i < array->len; i++) {
        printf("[%i] := ", i);
        print_value(&array->values[i], true);
        printf("\n");
    }
    printf("\n");
}

void disassemble_chunk(const Chunk *self)
{
    disassemble_constants(self);
    printf("[BYTECODE]: '%s'\n", self->name);
    for (int offset = 0; offset < self->len; ) {
        offset = disassemble_instruction(self, offset);
    }
    printf("\n");
}

// Note the side-effect!
#define read_byte(ip)   (*(ip)++)

// Strangely, the macro version will cause an unsequenced modification error.
// Yet `vm.c` continues to compile just fine!
static int read_byte3(const Byte *ip)
{
    Byte msb = read_byte(ip);
    Byte mid = read_byte(ip);
    Byte lsb = read_byte(ip);
    return encode_byte3(msb, mid, lsb);
}

#define read_constant(chunk, index) \
    ((chunk)->constants.values[(index)])

static void constant_op(const Chunk *chunk, const Byte *ip)
{
    int arg = read_byte3(ip);
    printf("Kst[%i] ; ", arg);
    print_value(&read_constant(chunk, arg), true);
}

// Assumes all instructions that manipulate ranges of values on the stack will
// only ever have a 1-byte argument.
static void range_op(const Byte *ip)
{
    int arg = read_byte(ip);
    printf("Top[%i...-1]", -arg);
}

static void simple_op(const char *action, const Byte *ip)
{
    int arg = read_byte(ip);
    printf("%s(%i)", action, arg);
}

static void local_op(const Byte *ip)
{
    int arg = read_byte(ip);
    printf("Loc[%i]", arg);
}

static void settable_op(const Byte *ip)
{
    int t_idx  = read_byte(ip);
    int k_idx  = read_byte(ip);
    int to_pop = read_byte(ip);
    printf("Tbl[%i] Key[%i] Pop(%i)", t_idx, k_idx, to_pop);
}

static void setarray_op(const Byte *ip)
{
    int t_idx  = read_byte(ip);
    int to_set = read_byte(ip);
    printf("Tbl[%i] Set(%i)", t_idx, to_set);
}

int disassemble_instruction(const Chunk *self, int offset)
{
    const Byte  *ip = &self->code[offset];
    const OpCode op = read_byte(ip);

    printf("%04x ", offset);
    int line = self->lines[offset];
    if (offset > 0 && line == self->lines[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4i ", line);
    }

    printf("%-16s ", get_opinfo(op).name);
    switch (op) {
    case OP_CONSTANT:
    case OP_GETGLOBAL:
    case OP_SETGLOBAL: constant_op(self, ip); break;
    case OP_GETLOCAL:
    case OP_SETLOCAL: local_op(ip); break;
    case OP_POP:      simple_op("Pop", ip); break;
    case OP_NIL:      simple_op("Nil", ip); break;
    case OP_CONCAT:
    case OP_PRINT:    range_op(ip); break;
    case OP_SETTABLE: settable_op(ip); break;
    case OP_SETARRAY: setarray_op(ip); break;
    case OP_GETTABLE:
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
    case OP_RETURN:  break;
    default:
        // Should not happen
        printf("Unknown opcode '%i'.\n", op);
        return offset + 1;
    }
    printf("\n");
    return offset + get_opinfo(op).argsz + 1;
}

#undef read_byte
#undef read_byte2
#undef read_byte3
#undef read_constant
