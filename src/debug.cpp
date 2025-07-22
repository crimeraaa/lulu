#include <stdio.h>

#include "debug.hpp"
#include "object.hpp"
#include "vm.hpp"

struct Args {
    struct ABC {u16 b, c;};

    u16 a;
    union {
        ABC abc;
        u32 bx;
        i32 sbx;
    };
};

[[gnu::format(printf, 4, 5)]]
static void
_print_reg(const Chunk *c, u16 reg, isize pc, const char *fmt = nullptr, ...)
{
    if (Instruction::reg_is_k(reg)) {
        u32 i = Instruction::reg_get_k(reg);
        value_print(c->constants[i]);
    } else {
        const char *id = chunk_get_local(c, cast_int(reg + 1), pc);
        if (id != nullptr) {
            printf("local %s", id);
        } else {
            printf("R(%u)", reg);
        }
    }

    if (fmt != nullptr) {
        va_list args;
        va_start(args, fmt);
        vfprintf(stdout, fmt, args);
        va_end(args);
    }
}

static void
_unary(const Chunk *c, const char *op, Args args, isize pc)
{
    _print_reg(c, args.a, pc, " := %s", op);
    _print_reg(c, args.abc.b, pc);
}

static void
_arith(const Chunk *c, char op, Args args, isize pc)
{
    _print_reg(c, args.a, pc, " := ");
    _print_reg(c, args.abc.b, pc, " %c ", op);
    _print_reg(c, args.abc.c, pc);
}

static isize
_jump_to(isize pc, i32 offset)
{
    // we add 1 because by the time an instruction is being decoded, the
    // `ip` would have been incremented already.
    return (pc + 1) + cast(isize)offset;
}

static isize
_jump_get(const Chunk *c, isize jump_pc)
{
    Instruction i = c->code[jump_pc];
    lulu_assert(i.op() == OP_JUMP);
    return _jump_to(jump_pc, i.sbx());
}

static void
_compare(const Chunk *c, const char *op, Args args, isize pc)
{
    _print_reg(c, args.abc.b, pc, " %s ", op);
    _print_reg(c, args.abc.c, pc,
        " ; goto .code[%" ISIZE_FMTSPEC " if %s else %" ISIZE_FMTSPEC "]",
        _jump_to(pc, 1), (args.a) ? "false" : "true", _jump_get(c, pc + 1));
}

static int
_count_digits(isize n)
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
    return _count_digits(len(c->code) - 1);
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
        args.abc.b = ip.b();
        args.abc.c = ip.c();
        printf("%-4u ", args.a);

        if (opinfo[op].b() != OPARG_UNUSED) {
            printf("%-4u ", args.abc.b);
        } else {
            printf(PAD4);
        }

        if (opinfo[op].c() != OPARG_UNUSED) {
            printf("%-4u ; ", args.abc.c);
        } else {
            printf(PAD4 "; ");
        }

        break;
    case OPFORMAT_ABX:
        args.bx = ip.bx();
        printf("%-4u %-4u " PAD4 "; ", args.a, args.bx);
        break;
    case OPFORMAT_ASBX:
        args.sbx = ip.sbx();
        printf("%-4u %-4u " PAD4 "; ", args.a, args.sbx);
        break;
    }

    switch (op) {
    case OP_CONSTANT:
        printf("R(%u) := ", args.a);
        value_print(c->constants[args.bx]);
        break;
    case OP_NIL:
        if (args.a == args.abc.b) {
            printf("R(%u) := nil", args.a);
        } else {
            printf("R(%u:%u) := nil", args.a, args.abc.b + 1);
        }
        break;
    case OP_BOOL:
        _print_reg(c, args.a, pc, " := %s", (args.abc.b) ? "true" : "false");
        if (args.abc.c) {
            printf("; goto .code[%" ISIZE_FMTSPEC "]", _jump_to(pc, 1));
        }
        break;
    case OP_GET_GLOBAL:
        _print_reg(c, args.a, pc, " := ");
        value_print(c->constants[args.bx]);
        break;

    case OP_SET_GLOBAL: {
        OString *s = c->constants[args.bx].to_ostring();
        char     q = (s->len == 1) ? '\'' : '\"';
        printf("_G[%c%s%c] := ", q, s->to_cstring(), q);
        _print_reg(c, args.a, pc);
        break;
    }
    case OP_NEW_TABLE: {
        _print_reg(c, args.a, pc, " := {}; #hash = %u, #array = %u", args.abc.b, args.abc.c);
        break;
    }
    case OP_GET_TABLE: {
        _print_reg(c, args.a, pc, " := ");
        _print_reg(c, args.abc.b, pc, "[");
        _print_reg(c, args.abc.c, pc, "]");
        break;
    }
    case OP_SET_TABLE: {
        _print_reg(c, args.a, pc, "[");
        _print_reg(c, args.abc.b, pc, "] := ");
        _print_reg(c, args.abc.c, pc);
        break;
    }
    case OP_MOVE:
        _print_reg(c, args.a, pc, " := ");
        _print_reg(c, args.abc.b, pc);
        break;
    case OP_ADD: _arith(c, '+', args, pc); break;
    case OP_SUB: _arith(c, '-', args, pc); break;
    case OP_MUL: _arith(c, '*', args, pc); break;
    case OP_DIV: _arith(c, '/', args, pc); break;
    case OP_MOD: _arith(c, '%', args, pc); break;
    case OP_POW: _arith(c, '^', args, pc); break;
    case OP_EQ:  _compare(c, "==", args, pc); break;
    case OP_LT:  _compare(c, "<",  args, pc); break;
    case OP_LEQ: _compare(c, "<=", args, pc); break;
    case OP_UNM: _unary(c, "-", args, pc); break;
    case OP_NOT: _unary(c, "not ", args, pc); break;
    case OP_LEN: _unary(c, "#", args, pc); break;
    case OP_CONCAT:
        _print_reg(c, args.a, pc, " := concat(R(%u:%u))", args.abc.b, args.abc.c + 1);
        break;
    case OP_TEST: {
        printf("goto .code[%" ISIZE_FMTSPEC " if %s", _jump_to(pc, 1), (args.abc.c) ? "not " : "");
        _print_reg(c, args.a, pc, " else %" ISIZE_FMTSPEC "]", _jump_get(c, pc + 1));
        break;
    }
    case OP_TEST_SET: {
        printf("if %s", (args.abc.c) ? "" : "not ");
        _print_reg(c, args.abc.b, pc, " then ");
        _print_reg(c, args.a, pc, " := ");
        _print_reg(c, args.abc.b, pc,
            "; goto .code[%" ISIZE_FMTSPEC "]; else goto .code[%" ISIZE_FMTSPEC "]",
            _jump_get(c, pc + 1),
            _jump_to(pc, 1));
        break;
    }
    case OP_JUMP: {
        i32 offset = args.sbx;
        printf("ip += %i ; goto .code[%" ISIZE_FMTSPEC "]", offset, _jump_to(pc, offset));
        break;
    }
    case OP_CALL: {
        u16 argc = args.abc.b;
        u16 retc = args.abc.c;

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
        printf("return R(%u:%u)", args.a, args.a + args.abc.b);
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
        int pad = _count_digits(len(c->locals));
        printf(".local:\n");
        for (isize i = 0, n = len(c->locals); i < n; i++) {
            const char *id = c->locals[i].identifier->to_cstring();
            printf("[%.*" ISIZE_FMTSPEC "] '%s': start=%" ISIZE_FMTSPEC ", end=%" ISIZE_FMTSPEC "\n",
                pad, i, id, c->locals[i].start_pc, c->locals[i].end_pc);
        }
        printf("\n");
    }

    if (len(c->constants) > 0) {
        int pad = _count_digits(len(c->constants));
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

/**
 * @brief
 *      Symbolically executes all instructions in `c->code[:target_pc]`.
 *      That is, we go through each instruction and examine what its
 *      side-effects would be to determine the glocal/local variable
 *      or table field that caused an error.
 *
 * @param c
 *      Where the bytecode is found.
 *
 * @param target_pc
 *      The index of the instruction that caused the error to be raised,
 *      e.g. `OP_ADD` when one of the operands is a non-number.
 *
 * @param reg
 *      The index in the VM stack of the value that caused the error.
 *      e.g. this could be the RK(B) in an `OP_ADD`.
 *
 * @return
 *      The `Instruction` that retrieved a culrpit variable, or else
 *      the neutral `OP_RETURN`.
 */
static Instruction
_get_variable_ip(const Chunk *p, isize target_pc, int reg)
{
    // Store position of last instruction that changed `reg`, defaulting
    // to the final 'neutral' return.
    isize last_pc = len(p->code) - 1;

    // If `target_pc` is `OP_ADD` then don't pseudo-execute it, as from this
    // point there is no more information we could possibly get.
    for (isize pc = 0; pc < target_pc; pc++) {
        Instruction i = p->code[pc];
        OpCode op = i.op();

        int a = cast_int(i.a());
        int b = 0;

        OpInfo info = opinfo[op];
        switch (info.fmt()) {
        case OPFORMAT_ABC:
            b = cast_int(i.b());
            break;
        case OPFORMAT_ABX:
        case OPFORMAT_ASBX:
            // Nothing else to be done; we will check the instruction mode
            // anyway and we don't need to retrieve variables nor jump anywhere.
            break;
        default:
            lulu_panicf("Invalid OpFormat(%i)", info.fmt());
            lulu_unreachable();
            break;
        }

        // This instruction uses R(A) as a destination?
        if (info.a()) {
            // `R(reg)` was changed by this instruction?
            if (reg == a) {
                last_pc = pc;
            }
        }

        switch (op) {
        case OP_NIL:
            //  `reg` is part of the range `a` up to `b`, which is set here?
            if (a <= reg && reg <= cast_int(b)) {
                last_pc = pc;
            }
            break;
        default:
            break;
        }
    }
    return p->code[last_pc];
}

static const char *
_get_rk_name(const Chunk *c, u16 regk)
{
    if (Instruction::reg_is_k(regk)) {
        u32   i = Instruction::reg_get_k(regk);
        Value v = c->constants[i];
        if (v.is_string()) {
            return v.to_cstring();
        }
    }
    return "?";
}

static const char *
_get_obj_name(lulu_VM *vm, Call_Frame *cf, int reg, const char **id)
{
    Closure *f = cf->function;
    if (closure_is_lua(f)) {
        Chunk *c  = f->lua.chunk;

        // `ip` always points to the instruction after the decoded one, so
        // subtract 1 to get the culprit.
        isize pc = ptr_index(c->code, vm->saved_ip) - 1;

        // Add 1 to `reg` because we want to use 1-based counting. E.g.
        // the very first local is 1 rather than 0.
        *id = chunk_get_local(c, reg + 1, pc);
        if (*id != nullptr) {
            return "local";
        }
        Instruction i = _get_variable_ip(c, pc, reg);
        switch (i.op()) {
        case OP_GET_GLOBAL: {
            u32 g = i.bx();
            lulu_assert(c->constants[g].is_string());
            *id = c->constants[g].to_cstring();
            return "global";
        }
        case OP_GET_TABLE: {
            u16 rkc = i.c(); // RK(C) is the desired field.
            *id = _get_rk_name(c, rkc);
            return "field";
        }
        default:
            break;
        }
    }

    // No useful name found.
    return nullptr;
}

void
debug_type_error(lulu_VM *vm, const char *act, const Value *v)
{
    const char *id    = nullptr;
    const char *scope = nullptr;
    const char *tname = v->type_name();

    isize i;
    // `v` is currently inside the stack?
    if (ptr_index_safe(vm->window, v, &i)) {
        scope = _get_obj_name(vm, vm->caller, cast_int(i), &id);
    }

    if (scope != nullptr) {
        vm_runtime_error(vm, "Attempt to %s %s '%s' (a %s value)",
            act, scope, id, tname);
    } else {
        vm_runtime_error(vm, "Attempt to %s a %s value", act, tname);
    }
}

void
debug_arith_error(lulu_VM *vm, const Value *a, const Value *b)
{
    const Value *v = a->is_number() ? b : a;
    debug_type_error(vm, "perform arithmetic on", v);
}

void
debug_compare_error(lulu_VM *vm, const Value *a, const Value *b)
{
    const char *tname = a->type_name();
    if (a->type() == b->type()) {
        /**
         * @note(2025-07-22)
         *      Not as helpful as the other error messages, but printing
         *      out a messages that can have 0 up to 2 variables is surprisingly
         *      tricky!
         */
        vm_runtime_error(vm, "Attempt to compare 2 %s values", tname);
    } else {
        vm_runtime_error(vm, "Attempt to compare %s with %s", tname, b->type_name());
    }
}
