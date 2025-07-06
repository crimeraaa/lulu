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
print_reg(const Chunk *c, u16 reg, int pc)
{
    if (reg_is_rk(reg)) {
        u32 i = reg_get_k(reg);
        value_print(c->constants[i]);
        return;
    }

    const char *id = debug_get_local(c, cast_int(reg + 1), pc);
    if (id != nullptr) {
        printf("local %s", id);
    } else {
        printf("R(%i)", reg);
    }
}

static void
unary(const Chunk *c, const char *op, Args args, int pc)
{
    print_reg(c, args.a, pc);
    printf(" := %s", op);
    print_reg(c, args.basic.b, pc);
}

static void
arith(const Chunk *c, char op, Args args, int pc)
{
    print_reg(c, args.a, pc);
    printf(" := ");
    print_reg(c, args.basic.b, pc);
    printf(" %c ", op);
    print_reg(c, args.basic.c, pc);
}

static void
compare(const Chunk *c, const char *op, Args args, int pc)
{
    printf("R(%i) = ", args.a);
    print_reg(c, args.basic.b, pc);
    printf(" %s ", op);
    print_reg(c, args.basic.c, pc);
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
debug_get_pad(const Chunk *c)
{
    // Should be impossible, but just in case
    if (len(c->code) == 0) {
        return 0;
    }
    return count_digits(len(c->code) - 1);
}

// 4 spaces plus an extra one to separate messages.
#define PAD4 "     "

void
debug_disassemble_at(const Chunk *c, int pc, int pad)
{
    Args        args;
    Instruction ip = c->code[pc];
    OpCode op = getarg_op(ip);
    args.a = getarg_a(ip);
    printf("[%0*i] ", pad, pc);

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
        value_print(c->constants[args.extended.bx]);
        break;
    case OP_LOAD_NIL: {
        printf("R(i) := nil for %i <= i <= %i", args.a, args.basic.b);
        break;
    }
    case OP_LOAD_BOOL: {
        printf("R(%i) := %s", args.a, (args.basic.b) ? "true" : "false");
        break;
    }
    case OP_GET_GLOBAL:
        print_reg(c, args.a, pc);
        printf(" := ");
        value_print(c->constants[args.extended.bx]);
        break;

    case OP_SET_GLOBAL: {
        OString *s = value_to_ostring(c->constants[args.extended.bx]);
        char     q = (s->len == 1) ? '\'' : '\"';
        printf("_G[%c%s%c] := R(%i)", q, ostring_to_cstring(s), q, args.a);
        break;
    }
    case OP_NEW_TABLE: {
        print_reg(c, args.a, pc);
        printf(" := {}; #hash = %i, #array = %i", args.basic.b, args.basic.c);
        break;
    }
    case OP_GET_TABLE: {
        print_reg(c, args.a, pc);
        printf(" := ");
        print_reg(c, args.basic.b, pc);
        printf("[");
        print_reg(c, args.basic.c, pc);
        printf("]");
        break;
    }
    case OP_SET_TABLE: {
        print_reg(c, args.a, pc);
        printf("[");
        print_reg(c, args.basic.b, pc);
        printf("] := ");
        print_reg(c, args.basic.c, pc);
        break;
    }
    case OP_MOVE:
        print_reg(c, args.a, pc);
        printf(" := ");
        print_reg(c, args.basic.b, pc);
        break;
    case OP_ADD: arith(c, '+', args, pc); break;
    case OP_SUB: arith(c, '-', args, pc); break;
    case OP_MUL: arith(c, '*', args, pc); break;
    case OP_DIV: arith(c, '/', args, pc); break;
    case OP_MOD: arith(c, '%', args, pc); break;
    case OP_POW: arith(c, '^', args, pc); break;
    case OP_EQ:  compare(c, "==", args, pc); break;
    case OP_LT:  compare(c, "<", args, pc); break;
    case OP_LEQ: compare(c, "<=", args, pc); break;
    case OP_UNM: unary(c, "-", args, pc); break;
    case OP_NOT: unary(c, "not ", args, pc); break;
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
debug_disassemble(const Chunk *c)
{
    printf("\n=== DISASSEMBLY: BEGIN ===\n");
    printf(".stack_used:\n%i\n\n", c->stack_used);
    if (len(c->locals) > 0) {
        int pad = count_digits(len(c->locals));
        printf(".local:\n");
        for (size_t i = 0, n = len(c->locals); i < n; i++) {
            Local local = c->locals[i];
            const char *id = ostring_to_cstring(local.identifier);
            printf("[%.*zu] '%s': start=%i, end=%i\n",
                pad, i, id, local.start_pc, local.end_pc);
        }
        printf("\n");
    }

    if (len(c->constants) > 0) {
        int pad = count_digits(len(c->constants));
        printf(".const:\n");
        for (size_t i = 0, n = len(c->constants); i < n; i++) {
            printf("[%.*zu] ", pad, i);
            value_print(c->constants[i]);
            printf("\n");
        }
        printf("\n");
    }

    printf(".code:\n");
    int pad = debug_get_pad(c);
    for (int i = 0, n = cast_int(len(c->code)); i < n; i++) {
        debug_disassemble_at(c, i, pad);
    }
    printf("\n=== DISASSEMBLY: END ===\n");
}

const char *
debug_get_local(const Chunk *c, int local_number, int pc)
{
    int counter = local_number;
    for (Local local : c->locals) {
        // nth local cannot possible be active at this point, and we assume
        // that all succeeding locals won't be either.
        if (local.start_pc > pc) {
            break;
        }

        // Local is valid in this range?
        if (pc <= local.end_pc) {
            counter--;
            // We iterated the correct number of times for this scope?
            if (counter == 0) {
                return ostring_to_cstring(local.identifier);
            }
        }
    }
    return nullptr;
}
