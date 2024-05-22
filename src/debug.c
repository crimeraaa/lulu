#include "debug.h"
#include "limits.h"

static void disassemble_constants(const Chunk *ck)
{
    const VArray *kst = &ck->constants;
    printf("[CONSTANTS]:\n");
    for (int i = 0; i < kst->len; i++) {
        printf("[%i] := ", i);
        print_value(&kst->values[i], true);
        printf("\n");
    }
    printf("\n");
}

void disassemble_chunk(const Chunk *ck)
{
    disassemble_constants(ck);
    printf("[BYTECODE]: '%s'\n", ck->name);
    for (int offset = 0; offset < ck->len; ) {
        offset = disassemble_instruction(ck, offset);
    }
    printf("\n");
}

// Note the side-effect!
#define read_byte(ip)   (*((ip)++))

// Strangely, the macro version will cause an unsequenced modification error.
// Yet `vm.c` continues to compile just fine!
static int read_byte3(const Byte *ip)
{
    Byte msb = read_byte(ip);
    Byte mid = read_byte(ip);
    Byte lsb = read_byte(ip);
    return encode_byte3(msb, mid, lsb);
}

#define read_constant(ck, i)    ((ck)->constants.values[(i)])

static void constant_op(const Chunk *ck, const Byte *ip)
{
    int arg = read_byte3(ip);
    printf("Kst[%i] ; ", arg);
    print_value(&read_constant(ck, arg), true);
}

static void simple_op(const char *act, const Byte *ip)
{
    int arg = read_byte(ip);
    printf("%s(%i)", act, arg);
}

static void local_op(const Byte *ip)
{
    int arg = read_byte(ip);
    printf("Loc[%i]", arg);
}

static void newtable_op(const Byte *ip)
{
    printf("Tbl(size = %i)", read_byte3(ip));
}

static void settable_op(const Byte *ip)
{
    int t_idx  = read_byte(ip);
    int k_idx  = read_byte(ip);
    int to_pop = read_byte(ip);
    printf("Tbl[%i], Key[%i], Pop(%i)", t_idx, k_idx, to_pop);
}

static void setarray_op(const Byte *ip)
{
    int t_idx  = read_byte(ip);
    int to_set = read_byte(ip);
    printf("Tbl[%i], Set(%i)", t_idx, to_set);
}

int disassemble_instruction(const Chunk *ck, int offset)
{
    const Byte  *ip = &ck->code[offset];
    const OpCode op = read_byte(ip);
    int          ln = ck->lines[offset];

    printf("%04x ", offset);
    if (offset > 0 && ln == ck->lines[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4i ", ln);
    }

    printf("%-16s ", get_opinfo(op).name);
    switch (op) {
    case OP_CONSTANT:
    case OP_GETGLOBAL:
    case OP_SETGLOBAL: constant_op(ck, ip);     break;
    case OP_GETLOCAL:
    case OP_SETLOCAL:  local_op(ip);            break;
    case OP_POP:       simple_op("Pop", ip);    break;
    case OP_NIL:       simple_op("Nil", ip);    break;
    case OP_NEWTABLE:  newtable_op(ip);         break;
    case OP_CONCAT:    simple_op("Concat", ip); break;
    case OP_PRINT:     simple_op("Print", ip);  break;
    case OP_SETTABLE:  settable_op(ip);         break;
    case OP_SETARRAY:  setarray_op(ip);         break;
    case OP_GETTABLE:
    case OP_TRUE:
    case OP_FALSE:
    case OP_EQ:
    case OP_LT:
    case OP_LE:
    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_DIV:
    case OP_MOD:
    case OP_POW:
    case OP_UNM:
    case OP_NOT:
    case OP_LEN:
    case OP_RETURN: break;
    }

    // Separating so compiler can catch unhandled cases while still handling
    // strange opcode errors
    if (op < 0 || op >= NUM_OPCODES) {
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
