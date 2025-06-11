#include <stdio.h>

#include "debug.hpp"

union Args_Extended {
    u32 bx;
    i32 sbx;
};

struct Args_Basic {
    u16 b, c;
};

// As space efficient as we can be without violating the standard, but like
// who cares?
struct Args {
    u8 a;
    union {
        Args_Extended extended;
        Args_Basic    basic;
    };
};

static void
print_reg(const Chunk &c, u16 reg)
{
    if (reg_is_rk(reg)) {
        u32 i = reg_get_k(reg);
        printf(LULU_NUMBER_FMT, c.constants[i]);
    } else {
        printf("R(%i)", reg);
    }
}

static void
arith(const Chunk &c, char op, Args args)
{
    printf("; R(%i) = ", args.a);
    print_reg(c, args.basic.b);
    printf(" %c ", op);
    print_reg(c, args.basic.c);
}

int
debug_get_pad(const Chunk &c)
{
    size_t n     = len(c.code);
    int    count = 0;
    while (n > 0) {
        n /= 10;
        count++;
    }
    return count;
}

void
debug_disassemble_at(const Chunk &c, Instruction ip, int pc, int pad)
{
    Args   args;
    OpCode op = getarg_op(ip);
    args.a = getarg_a(ip);
    printf("[%*i] ", pad, pc);

    int line = chunk_get_line(c, pc);
    // Have a previous line and it's the same as ours?
    if (pc > 0 && chunk_get_line(c, pc - 1) == line) {
        printf("   | ");
    } else {
        printf("%4i ", line);
    }

    printf("%-16s ", opcode_names[op]);
    switch (opinfo_fmt(op)) {
    case OPFORMAT_ABC:
        args.basic.b = getarg_b(ip);
        args.basic.c = getarg_c(ip);
        printf("%-4i %-4i %-4i ", args.a, args.basic.b, args.basic.c);
        break;
    case OPFORMAT_ABX:
        args.extended.bx = getarg_bx(ip);
        printf("%-4i %-4i %-4s ", args.a, args.extended.bx, "");
        break;
    case OPFORMAT_ASBX:
        args.extended.sbx = getarg_sbx(ip);
        printf("%-4i %-4i %-4s ", args.a, args.extended.sbx, "");
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


void
debug_disassemble(const Chunk &c)
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
    int pad = debug_get_pad(c);
    for (size_t i = 0, end = code.len; i < end; i++) {
        debug_disassemble_at(c, code[i], cast_int(i), pad);
    }
}
