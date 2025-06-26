#include <stdio.h>

#include "debug.hpp"
#include "object.hpp"

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
        value_print(c.constants[i]);
    } else {
        printf("R(%i)", reg);
    }
}

static void
unary(const Chunk &c, const char *op, Args args)
{
    printf("R(%i) := %s", args.a, op);
    print_reg(c, args.basic.b);
}

static void
arith(const Chunk &c, char op, Args args)
{
    printf("R(%i) = ", args.a);
    print_reg(c, args.basic.b);
    printf(" %c ", op);
    print_reg(c, args.basic.c);
}

static void
compare(const Chunk &c, const char *op, Args args)
{
    printf("R(%i) = ", args.a);
    print_reg(c, args.basic.b);
    printf(" %s ", op);
    print_reg(c, args.basic.c);
}

static int
count_digits(size_t n)
{
    int count = 0;
    while (n > 0) {
        n /= 10;
        count++;
    }
    return count;
}

int
debug_get_pad(const Chunk &c)
{
    return count_digits(len(c.code));
}

// 4 spaces plus an extra one to separate messages.
#define PAD4 "     "

void
debug_disassemble_at(const Chunk &c, int pc, int pad)
{
    Args        args;
    Instruction ip = c.code[pc];
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
        printf("%-4i %-4i ", args.a, args.basic.b);
        if (opinfo_c(op) != OPARG_UNUSED) {
            printf("%-4i ; ", args.basic.c);
        } else {
            printf(PAD4 "; ");
        }
        break;
    case OPFORMAT_ABX:
        args.extended.bx = getarg_bx(ip);
        printf("%-4i %-4i " PAD4 "; ", args.a, args.extended.bx);
        break;
    case OPFORMAT_ASBX:
        args.extended.sbx = getarg_sbx(ip);
        printf("%-4i %-4i " PAD4 "; ", args.a, args.extended.sbx);
        break;
    }

    switch (op) {
    case OP_CONSTANT:
        printf("R(%i) := ", args.a);
        value_print(c.constants[getarg_bx(ip)]);
        break;
    case OP_LOAD_NIL: {
        printf("R(i) := nil for %i <= i <= %i", args.a, args.basic.b);
        break;
    }
    case OP_LOAD_BOOL: {
        printf("R(%i) := %s", args.a, (args.basic.b) ? "true" : "false");
        break;
    }
    case OP_GET_GLOBAL: {
        print_reg(c, args.a);
        printf(" := ");
        value_print(c.constants[getarg_bx(ip)]);
        break;
    }

    case OP_SET_GLOBAL: {
        OString *s = &c.constants[getarg_bx(ip)].object->ostring;
        char     q = (s->len == 1) ? '\'' : '\"';
        printf("_G[%c%s%c] := R(%i)", q, s->data, q, args.a);
        break;
    }
    case OP_ADD: arith(c, '+', args); break;
    case OP_SUB: arith(c, '-', args); break;
    case OP_MUL: arith(c, '*', args); break;
    case OP_DIV: arith(c, '/', args); break;
    case OP_MOD: arith(c, '%', args); break;
    case OP_POW: arith(c, '^', args); break;
    case OP_EQ:  compare(c, "==", args); break;
    case OP_LT:  compare(c, "<", args); break;
    case OP_LEQ: compare(c, "<=", args); break;
    case OP_UNM: unary(c, "-", args); break;
    case OP_NOT: unary(c, "not ", args); break;
    case OP_CONCAT:
        printf("R(%i) := concat(R(%i:%i))",
            args.a, args.basic.b, args.basic.c + 1);
        break;
    case OP_CALL: {
        u16 argc = args.basic.b;
        u16 retc = args.basic.c;

        u16 last_ret = args.a + retc;
        if (args.a != last_ret) {
            printf("R(%i:%i) := ", args.a, last_ret);
        }

        u16 first_arg = args.a + 1;
        u16 last_arg  = first_arg + argc;
        if (first_arg == last_arg) {
            printf("R(%i)()", args.a);
        } else {
            printf("R(%i)(R(%i:%i))", args.a, first_arg, last_arg);
        }
        break;
    }
    case OP_RETURN:
        printf("return R(%i:%i)", args.a, u16(args.a) + args.basic.b);
        break;
    }

    printf("\n");
}


void
debug_disassemble(const Chunk &c)
{
    const auto &constants = c.constants;
    printf("\n=== DISASSEMBLY: BEGIN ===\n");
    printf(".stack_used:\n%i\n\n", c.stack_used);
    if (len(constants) > 0) {
        int pad = count_digits(len(constants));
        printf(".const:\n");
        for (size_t i = 0, end = len(constants); i < end; i++) {
            printf("[%.*zu] ", pad, i);
            value_print(constants[i]);
            printf("\n");
        }
        printf("\n");
    }

    const auto &code = c.code;
    printf(".code:\n");
    int pad = debug_get_pad(c);
    for (int i = 0, end = cast_int(len(code)); i < end; i++) {
        debug_disassemble_at(c, i, pad);
    }
    printf("\n=== DISASSEMBLY: END ===\n");
}
