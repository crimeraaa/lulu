#include <stdio.h>

#define DYNAMIC_IMPLEMENTATION
#include "chunk.h"

void
chunk_init(Chunk &c)
{
    dynamic_init(c.code);
    dynamic_init(c.constants);
    c.stack_used = 2; // R(0) and R(1) must always be valid.
}

void
chunk_append(lulu_VM &vm, Chunk &c, Instruction i)
{
    dynamic_push(vm, c.code, i);
}

u32
chunk_add_constant(lulu_VM &vm, Chunk &c, Value v)
{
    auto &a = c.constants;
    for (size_t i = 0, end = a.len; i < end; i++) {
        if (a[i] == v) {
            return cast(u32, i);
        }
    }

    dynamic_push(vm, a, v);
    return cast(u32, a.len - 1);
}

void
chunk_destroy(lulu_VM &vm, Chunk &c)
{
    dynamic_delete(vm, c.code);
    dynamic_delete(vm, c.constants);
}


void
chunk_dump_all(const Chunk &c)
{
    const auto &constants = c.constants;
    if (constants.len > 0) {
        printf(".const:\n");
        for (size_t i = 0, end = constants.len; i < end; i++) {
            printf("[%zu] " LULU_NUMBER_FMT "\n", i, constants[i]);
        }
        printf("\n");
    }

    const auto &code = c.code;
    printf(".code:\n");
    for (size_t i = 0, end = code.len; i < end; i++) {
        chunk_dump_instruction(c, code[i], cast_int(i));
    }
}

typedef struct {
    OpCode op;
    u8     a;
    union {
        struct {u16 b, c;};
        u32 bx;
        i32 sbx;
    };
} Args;

static void
print_reg(const Chunk &c, u16 reg)
{
    if (rk_is_rk(reg)) {
        u32 i = rk_get_k(reg);
        printf(LULU_NUMBER_FMT, c.constants[i]);
    } else {
        printf("R(%i)", reg);
    }
}

static void
arith(const Chunk &c, char op, const Args &args)
{
    printf("; R(%i) = ", args.a);
    print_reg(c, args.b);
    printf(" %c ", op);
    print_reg(c, args.c);
}

void
chunk_dump_instruction(const Chunk &c, Instruction ip, int pc)
{
    Args   args;
    OpCode op = getarg_op(ip);
    args.a = getarg_a(ip);

    printf("[%i] %-16s ", pc, opcode_names[op]);
    switch (OPINFO_FMT(op)) {
    case OPFORMAT_ABC:
        args.b = getarg_b(ip);
        args.c = getarg_c(ip);
        printf("%-4i %-4i %-4i ", args.a, args.b, args.c);
        break;
    case OPFORMAT_ABX:
        args.bx = getarg_bx(ip);
        printf("%-4i %-4i ", args.a, args.bx);
        break;
    case OPFORMAT_ASBX:
        args.sbx = getarg_sbx(ip);
        printf("%-4i %-4i ", args.a, args.sbx);
        break;
    }

    switch (op) {
    case OP_LOAD_CONSTANT:
        printf("; R(%i) := .const[%i]", args.a, getarg_bx(ip));
        break;
    case OP_ADD: arith(c, '+', args); break;
    case OP_SUB: arith(c, '-', args); break;
    case OP_MUL: arith(c, '*', args); break;
    case OP_DIV: arith(c, '/', args); break;
    case OP_MOD: arith(c, '%', args); break;
    case OP_POW: arith(c, '^', args); break;
    case OP_RETURN:
        break;
    }

    printf("\n");
}
