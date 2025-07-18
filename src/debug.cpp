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
    u16 a;
    union {
        Args_Extended extended;
        Args_Basic    basic;
    };
};

static void
print_reg(const Chunk *c, u16 reg, isize pc)
{
    if (Instruction::reg_is_rk(reg)) {
        u32 i = Instruction::reg_get_k(reg);
        value_print(c->constants[i]);
        return;
    }

    const char *id = chunk_get_local(c, cast_int(reg + 1), pc);
    if (id != nullptr) {
        printf("local %s", id);
    } else {
        printf("R(%u)", reg);
    }
}

static void
unary(const Chunk *c, const char *op, Args args, isize pc)
{
    print_reg(c, args.a, pc);
    printf(" := %s", op);
    print_reg(c, args.basic.b, pc);
}

static void
arith(const Chunk *c, char op, Args args, isize pc)
{
    print_reg(c, args.a, pc);
    printf(" := ");
    print_reg(c, args.basic.b, pc);
    printf(" %c ", op);
    print_reg(c, args.basic.c, pc);
}

static isize
jump_to(isize pc, i32 offset = 1)
{
    return (pc + 1) + cast(isize)offset;
}

static isize
jump_to(const Chunk *c, isize jump_pc)
{
    Instruction i = c->code[jump_pc];
    lulu_assert(i.op() == OP_JUMP);
    return jump_to(jump_pc, i.sbx());
}

static void
compare(const Chunk *c, const char *op, Args args, isize pc)
{
    print_reg(c, args.basic.b, pc);
    printf(" %s ", op);
    print_reg(c, args.basic.c, pc);

    printf(" ; goto .code[%" ISIZE_FMTSPEC " if %s else %" ISIZE_FMTSPEC "]",
        jump_to(pc, 1), (args.a) ? "true" : "false", jump_to(c, pc + 1));
}

static int
count_digits(isize n)
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
        return 1;
    }
    return count_digits(len(c->code) - 1);
}

// 4 spaces plus an extra one to separate messages.
#define PAD4 "     "

void
debug_disassemble_at(const Chunk *c, Instruction ip, isize pc, int pad)
{
    Args   args;
    OpCode op = ip.op();
    args.a = ip.a();
    printf("[%0*" ISIZE_FMTSPEC "] ", pad, pc);

    int line = chunk_get_line(c, pc);
    // Have a previous line and it's the same as ours?
    if (pc > 0 && chunk_get_line(c, pc - 1) == line) {
        printf("   | ");
    } else {
        printf("%4i ", line);
    }

    printf("%-16s ", opnames[op]);
    switch (opinfo[op].fmt()) {
    case OPFORMAT_ABC:
        args.basic.b = ip.b();
        args.basic.c = ip.c();
        printf("%-4i %-4i ", args.a, args.basic.b);
        if (opinfo[op].c() != OPARG_UNUSED) {
            printf("%-4i ; ", args.basic.c);
        } else {
            printf(PAD4 "; ");
        }
        break;
    case OPFORMAT_ABX:
        args.extended.bx = ip.bx();
        printf("%-4i %-4i " PAD4 "; ", args.a, args.extended.bx);
        break;
    case OPFORMAT_ASBX:
        args.extended.sbx = ip.sbx();
        printf("%-4i %-4i " PAD4 "; ", args.a, args.extended.sbx);
        break;
    }

    switch (op) {
    case OP_CONSTANT:
        printf("R(%u) := ", args.a);
        value_print(c->constants[args.extended.bx]);
        break;
    case OP_LOAD_NIL:
        printf("R(%u:%u) := nil ", args.a, args.basic.b + 1);
        break;
    case OP_LOAD_BOOL:
        printf("R(%u) := %s", args.a, (args.basic.b) ? "true" : "false");
        if (args.basic.c) {
            printf(" then goto [%" ISIZE_FMTSPEC "]", jump_to(pc));
        }
        break;
    case OP_GET_GLOBAL:
        print_reg(c, args.a, pc);
        printf(" := ");
        value_print(c->constants[args.extended.bx]);
        break;

    case OP_SET_GLOBAL: {
        OString *s = value_to_ostring(c->constants[args.extended.bx]);
        char     q = (s->len == 1) ? '\'' : '\"';
        printf("_G[%c%s%c] := ", q, ostring_to_cstring(s), q);
        print_reg(c, args.a, pc);
        break;
    }
    case OP_NEW_TABLE: {
        print_reg(c, args.a, pc);
        printf(" := {}; #hash = %u, #array = %u", args.basic.b, args.basic.c);
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
    case OP_LT:  compare(c, "<",  args, pc); break;
    case OP_LEQ: compare(c, "<=", args, pc); break;
    case OP_UNM: unary(c, "-", args, pc); break;
    case OP_NOT: unary(c, "not ", args, pc); break;
    case OP_CONCAT:
        print_reg(c, args.a, pc);
        printf(" := concat(R(%u:%u))", args.basic.b, args.basic.c + 1);
        break;
    case OP_TEST: {
        // Concept check: given `C == 0`, when do we skip the next instruction?
        // How about for `C == 1`?
        printf("if %s", (args.basic.c) ? "not " : "");
        print_reg(c, args.a, pc);
        printf(" then goto .code[%" ISIZE_FMTSPEC "]", jump_to(pc));
        break;
    }
    case OP_TEST_SET: {
        printf("if %s", (args.basic.c) ? "" : "not ");
        print_reg(c, args.basic.b, pc);
        printf(" then ");
        print_reg(c, args.a, pc);
        printf(" := ");
        print_reg(c, args.basic.b, pc);
        printf(" else goto .code[%" ISIZE_FMTSPEC "]", jump_to(pc));
        break;
    }
    case OP_JUMP: {
        i32 offset = args.extended.sbx;
        // we add 1 because by the time an instruction is being decoded, the
        // `ip` would have been incremented already.
        printf("ip += %i ; goto [%" ISIZE_FMTSPEC "]", offset, jump_to(pc, offset));
        break;
    }
    case OP_CALL: {
        u16 argc = args.basic.b;
        u16 retc = args.basic.c;

        u16 last_ret = args.a + retc;
        if (retc == VARARG) {
            printf("R(%u:) := ", args.a);
        } else if (args.a != last_ret) {
            printf("R(%u:%u) := ", args.a, last_ret);
        }

        u16 first_arg = args.a + 1;
        u16 last_arg  = first_arg + argc;
        printf("R(%u)", args.a);
        if (first_arg == last_arg) {
            printf("()");
        } else if (argc == VARARG) {
            printf("(R(%u:))", first_arg);
        } else {
            printf("(R(%u:%u))", first_arg, last_arg);
        }
        break;
    }
    case OP_RETURN:
        printf("return R(%u:%u)", args.a, args.a + args.basic.b);
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
        for (isize i = 0, n = len(c->locals); i < n; i++) {
            const char *id = ostring_to_cstring(c->locals[i].identifier);
            printf("[%.*" ISIZE_FMTSPEC "] '%s': start=%" ISIZE_FMTSPEC ", end=%" ISIZE_FMTSPEC "\n",
                pad, i, id, c->locals[i].start_pc, c->locals[i].end_pc);
        }
        printf("\n");
    }

    if (len(c->constants) > 0) {
        int pad = count_digits(len(c->constants));
        printf(".const:\n");
        for (isize i = 0, n = len(c->constants); i < n; i++) {
            printf("[%.*" ISIZE_FMTSPEC "] ", pad, i);
            value_print(c->constants[i]);
            printf("\n");
        }
        printf("\n");
    }

    printf(".code:\n");
    int pad = debug_get_pad(c);
    for (isize i = 0, n = len(c->code); i < n; i++) {
        debug_disassemble_at(c, c->code[i], i, pad);
    }
    printf("\n=== DISASSEMBLY: END ===\n");
}
