#include "debug.h"
#include "limits.h"
#include "vm.h"

static void disassemble_constants(const Chunk *c)
{
    const Array *k = &c->constants;
    printf("[CONSTANTS]:\n");
    for (int i = 0; i < k->length; i++) {
        printf("[%i] := ", i);
        luluDbg_print_value(&k->values[i]);
        printf("\n");
    }
    printf("\n");
}

void luluDbg_print_stack(lulu_VM *vm)
{
    printf("\t[ ");
    for (StackID slot = vm->stack; slot < vm->top; slot++) {
        if (slot > vm->stack)
            printf(", ");
        luluDbg_print_value(slot);
    }
    printf(" ]\n");
}

void luluDbg_print_value(const Value *v)
{
    if (is_string(v)) {
        const String *s = as_string(v);
        const char    q = (s->length == 1) ? '\'' : '\"'; // fake char literals
        printf("%c%s%c", q, s->data, q);
    } else {
        char buf[LULU_MAX_TOSTRING];
        printf("%s", luluVal_to_string(v, buf));
    }
}

void luluDbg_disassemble_chunk(const Chunk *c)
{
    disassemble_constants(c);
    printf("[BYTECODE]: '%s'\n", c->name->data);
    for (int offset = 0; offset < c->length; ) {
        offset = luluDbg_disassemble_instruction(c, offset);
    }
    printf("\n");
}

// Note the side-effect!
#define read_byte(ip)   (*((ip)++))

// Strangely, the macro version will cause an unsequenced modification error.
// Yet `vm.c` continues to compile just fine!
static Byte3 read_byte3(const Byte *ip)
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
    luluDbg_print_value(&read_constant(ck, arg));
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

static SByte3 get_jump(Byte3 b3)
{
    // Sign bit is toggled?
    if (b3 & MIN_SBYTE3)
         // Throw away sign bit, negate raw value.
        return -(b3 & MAX_SBYTE3);
    else
        return b3;
}

static void print_jump(int from, char sign, int jump, int to)
{
    printf("ip = %04x %c %04x ; goto %04x", from, sign, jump, to);
}

static void jump_op(const Byte *ip, int offset)
{
    // `ip` points to OP_JUMP itself, so need to adjust.
    Byte3  arg  = read_byte3(ip);
    // Account for the adjustment `read_byte3()` normally does to VM ip.
    SByte3 jump = get_jump(arg) + get_opsize(OP_JUMP);
    int    addr = offset + jump;
    char   sign = (jump >= 0) ? '+' : '-';
    int    raw  = (jump >= 0) ? jump : -jump;
    print_jump(offset + 1, sign, raw, addr);
}

// Remember that normally the VM increments ip EVERY read_byte().
static void test_op(int offset)
{
    int jump = get_opsize(OP_JUMP);
    int addr = offset + jump + get_opsize(OP_TEST);
    printf("if <cond> ");
    print_jump(offset + 1, '+', jump, addr);
}

static void for_loop(int offset)
{
    int jump = get_opsize(OP_JUMP);
    int addr = offset + jump + get_opsize(OP_FORLOOP);
    printf("if !<loop> ");
    print_jump(offset + 1, '+', jump, addr);
}

int luluDbg_disassemble_instruction(const Chunk *c, int offset)
{
    const Byte  *ip = &c->code[offset];
    const OpCode op = read_byte(ip);
    const int    ln = c->lines[offset];

    printf("%04x ", offset);
    if (offset > 0 && ln == c->lines[offset - 1])
        printf("   | ");
    else
        printf("%4i ", ln);
    printf("%-16s ", get_opinfo(op).name);

    switch (op) {
    case OP_CONSTANT:
    case OP_GETGLOBAL:
    case OP_SETGLOBAL: constant_op(c, ip);     break;
    case OP_GETLOCAL:
    case OP_SETLOCAL:  local_op(ip);            break;
    case OP_POP:       simple_op("Pop", ip);    break;
    case OP_NIL:       simple_op("Nil", ip);    break;
    case OP_NEWTABLE:  newtable_op(ip);         break;
    case OP_CONCAT:    simple_op("Concat", ip); break;
    case OP_PRINT:     simple_op("Print", ip);  break;
    case OP_SETTABLE:  settable_op(ip);         break;
    case OP_SETARRAY:  setarray_op(ip);         break;
    case OP_TEST:      test_op(offset);         break;
    case OP_JUMP:      jump_op(ip, offset);     break;
    case OP_FORLOOP:   for_loop(offset);        break;
    case OP_FORPREP:
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
    return offset + get_opsize(op);
}

#undef read_byte
#undef read_byte2
#undef read_byte3
#undef read_constant
