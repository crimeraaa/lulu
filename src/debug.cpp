#include <stdio.h>

#include "debug.hpp"
#include "object.hpp"
#include "vm.hpp"

struct Args {
    isize pc;
    u16 a, b;
    union {
        u16 c;
        u32 bx;
        i32 sbx;
    };
};

[[gnu::format(printf, 4, 5)]]
static void
print_reg(const Chunk *p, u16 reg, isize pc, const char *fmt = nullptr, ...)
{
    if (Instruction::reg_is_k(reg)) {
        u32 i = Instruction::reg_get_k(reg);
        value_print(p->constants[i]);
    } else {
        const char *ident = chunk_get_local(p, cast_int(reg + 1), pc);
        if (ident != nullptr) {
            printf("local %s", ident);
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
unary(const Chunk *p, const char *op, const Args &args)
{
    isize pc = args.pc;
    print_reg(p, args.a, pc, " := %s", op);
    print_reg(p, args.b, pc);
}

static void
arith(const Chunk *p, char op, const Args &args)
{
    isize pc = args.pc;
    print_reg(p, args.a, pc, " := ");
    print_reg(p, args.b, pc, " %c ", op);
    print_reg(p, args.c, pc);
}

static isize
jump_resolve(isize pc, i32 offset)
{
    // we add 1 because by the time an instruction is being decoded, the
    // `ip` would have been incremented already.
    return (pc + 1) + cast(isize)offset;
}

static isize
jump_get(const Chunk *p, isize jump_pc)
{
    Instruction i = p->code[jump_pc];
    lulu_assert(i.op() == OP_JUMP);
    return jump_resolve(jump_pc, i.sbx());
}

static void
compare(const Chunk *p, const char *op, const Args &args)
{
    isize pc = args.pc;
    print_reg(p, args.b, pc, " %s ", op);
    print_reg(p, args.c, pc,
        " ; goto .code[%" ISIZE_FMT " if %s else %" ISIZE_FMT "]",
        jump_resolve(pc, 1), (args.a) ? "false" : "true", jump_get(p, pc + 1));
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
debug_get_pad(const Chunk *p)
{
    // Should be impossible, but just in case
    if (len(p->code) == 0) {
        return 1;
    }
    return count_digits(len(p->code) - 1);
}

// 4 spaces plus an extra one to separate messages.
#define PAD4 "     "

void
debug_disassemble_at(const Chunk *p, Instruction ip, isize pc, int pad)
{
    Args   args;
    OpCode op = ip.op();
    args.pc = pc;
    args.a  = ip.a();
    printf("[%0*" ISIZE_FMT "] ", pad, pc);

    int line = chunk_get_line(p, pc);
    // Have a previous line and it's the same as ours?
    if (pc > 0 && chunk_get_line(p, pc - 1) == line) {
        printf("   | ");
    } else {
        printf("%4i ", line);
    }

    printf("%-16s ", opnames[op]);
    switch (opinfo[op].fmt()) {
    case OPFORMAT_ABC:
        args.b = ip.b();
        args.c = ip.c();
        printf("%-4u ", args.a);

        if (opinfo[op].b() != OPARG_UNUSED) {
            printf("%-4u ", args.b);
        } else {
            printf(PAD4);
        }

        if (opinfo[op].c() != OPARG_UNUSED) {
            printf("%-4u ; ", args.c);
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
        printf("%-4u %-4i " PAD4 "; ", args.a, args.sbx);
        break;
    }

    switch (op) {
    case OP_MOVE:
        print_reg(p, args.a, pc, " := ");
        print_reg(p, args.b, pc);
        break;
    case OP_CONSTANT:
        printf("R(%u) := ", args.a);
        value_print(p->constants[args.bx]);
        break;
    case OP_NIL:
        if (args.a == args.b) {
            printf("R(%u) := nil", args.a);
        } else {
            printf("R(%u:%u) := nil", args.a, args.b + 1);
        }
        break;
    case OP_BOOL:
        print_reg(p, args.a, pc, " := %s", (args.b) ? "true" : "false");
        if (args.c) {
            printf("; goto .code[%" ISIZE_FMT "]", jump_resolve(pc, 1));
        }
        break;
    case OP_GET_GLOBAL:
        print_reg(p, args.a, pc, " := ");
        value_print(p->constants[args.bx]);
        break;

    case OP_SET_GLOBAL: {
        OString *s = p->constants[args.bx].to_ostring();
        char     q = (s->len == 1) ? '\'' : '\"';
        printf("_G[%c%s%c] := ", q, s->to_cstring(), q);
        print_reg(p, args.a, pc);
        break;
    }
    case OP_NEW_TABLE: {
        isize n_hash = floating_byte_decode(args.b);
        isize n_array = floating_byte_decode(args.c);
        print_reg(p, args.a, pc,
            " := {}; #hash = %" ISIZE_FMT ", #array = %" ISIZE_FMT,
            n_hash, n_array);
        break;
    }
    case OP_GET_TABLE: {
        print_reg(p, args.a, pc, " := ");
        print_reg(p, args.b, pc, "[");
        print_reg(p, args.c, pc, "]");
        break;
    }
    case OP_SET_TABLE: {
        print_reg(p, args.a, pc, "[");
        print_reg(p, args.b, pc, "] := ");
        print_reg(p, args.c, pc);
        break;
    }
    case OP_ADD: arith(p, '+', args); break;
    case OP_SUB: arith(p, '-', args); break;
    case OP_MUL: arith(p, '*', args); break;
    case OP_DIV: arith(p, '/', args); break;
    case OP_MOD: arith(p, '%', args); break;
    case OP_POW: arith(p, '^', args); break;
    case OP_EQ:  compare(p, "==", args); break;
    case OP_LT:  compare(p, "<",  args); break;
    case OP_LEQ: compare(p, "<=", args); break;
    case OP_UNM: unary(p, "-", args); break;
    case OP_NOT: unary(p, "not ", args); break;
    case OP_LEN: unary(p, "#", args); break;
    case OP_CONCAT:
        print_reg(p, args.a, pc, " := concat(R(%u:%u))", args.b, args.c + 1);
        break;
    case OP_TEST: {
        printf("goto .code[%" ISIZE_FMT " if %s", jump_resolve(pc, 1), (args.c) ? "not " : "");
        print_reg(p, args.a, pc, " else %" ISIZE_FMT "]", jump_get(p, pc + 1));
        break;
    }
    case OP_TEST_SET: {
        printf("if %s", (args.c) ? "" : "not ");
        print_reg(p, args.b, pc, " then ");
        print_reg(p, args.a, pc, " := ");
        print_reg(p, args.b, pc,
            "; goto .code[%" ISIZE_FMT "]; else goto .code[%" ISIZE_FMT "]",
            jump_get(p, pc + 1),
            jump_resolve(pc, 1));
        break;
    }
    case OP_FOR_PREP: {
        i32 offset = ip.sbx();
        printf("goto .code[%" ISIZE_FMT "]", jump_resolve(pc, offset));
        break;
    }
    case OP_FOR_LOOP: {
        i32 offset = ip.sbx();
        printf("goto .code[%" ISIZE_FMT "] if loop", jump_resolve(pc, offset));
        break;
    }
    case OP_JUMP: {
        i32 offset = args.sbx;
        printf("ip += %i ; goto .code[%" ISIZE_FMT "]",
            offset, jump_resolve(pc, offset));
        break;
    }
    case OP_CALL: {
        u16 argc = args.b;
        u16 retc = args.c;

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
        printf("return R(%u:%u)", args.a, args.a + args.b);
        break;
    }

    printf("\n");
}

void
debug_disassemble(const Chunk *p)
{
    printf("\n=== DISASSEMBLY: BEGIN ===\n");
    printf(".stack_used:\n%i\n\n", p->stack_used);
    if (len(p->locals) > 0) {
        int pad = count_digits(len(p->locals));
        printf(".local:\n");
        for (isize i = 0, n = len(p->locals); i < n; i++) {
            Local local = p->locals[i];
            const char *ident = local.ident->to_cstring();
            printf("[%.*" ISIZE_FMT "] '%s': start=%" ISIZE_FMT ", end=%" ISIZE_FMT "\n",
                pad, i, ident, local.start_pc, local.end_pc);
        }
        printf("\n");
    }

    if (len(p->constants) > 0) {
        int pad = count_digits(len(p->constants));
        printf(".const:\n");
        for (isize i = 0, n = len(p->constants); i < n; i++) {
            printf("[%.*" ISIZE_FMT "] ", pad, i);
            value_print(p->constants[i]);
            printf("\n");
        }
        printf("\n");
    }

    printf(".code:\n");
    int pad = debug_get_pad(p);
    for (isize i = 0, n = len(p->code); i < n; i++) {
        debug_disassemble_at(p, p->code[i], i, pad);
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
 * @param p
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
get_variable_ip(const Chunk *p, isize target_pc, int reg)
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
        case OP_JUMP: {
            isize target = cast_isize(pc) + 1 + cast_isize(b);
            // jump goes forward and does not skip `last_pc`?
            if (pc <= target && target <= last_pc) {
                // Skip unnecessary code.
                pc += cast_isize(b);
            }
            break;
        }
        default:
            break;
        }
    }
    return p->code[last_pc];
}

static const char *
get_rk_name(const Chunk *p, u16 regk)
{
    if (Instruction::reg_is_k(regk)) {
        u32   i = Instruction::reg_get_k(regk);
        Value v = p->constants[i];
        if (v.is_string()) {
            return v.to_cstring();
        }
    }
    return "?";
}

static const char *
get_obj_name(lulu_VM *vm, Call_Frame *cf, int reg, const char **ident)
{
    if (cf->is_lua()) {
        const Chunk *p = cf->to_lua()->chunk;

        // `ip` always points to the instruction after the decoded one, so
        // subtract 1 to get the culprit.
        isize pc = ptr_index(p->code, vm->saved_ip) - 1;

        // Add 1 to `reg` because we want to use 1-based counting. E.g.
        // the very first local is 1 rather than 0.
        *ident = chunk_get_local(p, reg + 1, pc);
        if (*ident != nullptr) {
            return "local";
        }
        Instruction i = get_variable_ip(p, pc, reg);
        switch (i.op()) {
        case OP_GET_GLOBAL: {
            Value k = p->constants[i.bx()];
            lulu_assert(k.is_string());
            *ident = k.to_cstring();
            return "global";
        }
        case OP_GET_TABLE: {
            u16 rkc = i.c(); // RK(C) is the desired field.
            *ident = get_rk_name(p, rkc);
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
    const char *ident = nullptr;
    const char *scope = nullptr;
    const char *tname = v->type_name();

    isize i;
    // `v` is currently inside the stack?
    if (ptr_index_safe(vm->window, v, &i)) {
        scope = get_obj_name(vm, vm->caller, cast_int(i), &ident);
    }

    if (scope != nullptr) {
        vm_runtime_error(vm, "Attempt to %s %s '%s' (a %s value)",
            act, scope, ident, tname);
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

static void
get_func_info(lulu_Debug *ar, Closure *f)
{
    if (f->is_c()) {
        ar->source          = "=[C]";
        ar->namewhat        = "C";
        ar->linedefined     = -1;
        ar->lastlinedefined = -1;
    } else {
        Chunk *p = f->lua.chunk;
        ar->source          = p->source->to_cstring();
        ar->namewhat        = (p->line_defined == 0) ? "main" : "lua";
        ar->linedefined     = p->line_defined;
        ar->lastlinedefined = p->last_line_defined;
    }
}


/**
 * @brief
 *      Gets the index of the current instruction, which is equivalent to
 *      `vm->saved_ip - 1`. Recall that `ip` always points to the instruction
 *      after the one we just decoded.
 */
static isize
get_current_pc(lulu_VM *vm, Call_Frame *cf)
{
    if (!cf->is_lua()) {
        return -1;
    }
    if (cf == raw_data(vm->frames.data)) {
        cf->saved_ip = vm->saved_ip;
    }
    // Subtract 1 because ip always points to after the desired instruction.
    return cf->saved_ip - raw_data(cf->to_lua()->chunk->code) - 1;
}

static const char *
get_func_name(lulu_VM *vm, Call_Frame *cf, const char **name)
{
    // The parent caller of this function MUST be lua.
    if (!(cf - 1)->is_lua()) {
        return nullptr;
    }
    // Point to parent caller (the call*ing* function).
    cf--;
    isize pc = get_current_pc(vm, cf);
    Instruction i = cf->to_lua()->chunk->code[pc];
    if (i.op() == OP_CALL) {
        return get_obj_name(vm, cf, i.a(), name);
    }
    // No useful name can be found.
    return nullptr;
}

static int
get_line(lulu_VM *vm, Call_Frame *cf)
{
    isize pc = get_current_pc(vm, cf);
    // Not a lua function, so no line information?
    if (pc < 0) {
        return -1;
    }
    return chunk_get_line(cf->to_lua()->chunk, pc);
}

static bool
get_info(lulu_VM *vm, const char *options, lulu_Debug *ar, Closure *f,
    Call_Frame *cf)
{
    bool status = true;
    for (const char *it = options; *it != '\0'; it++) {
        switch (*it) {
        case 'S':
            get_func_info(ar, f);
            break;
        case 'l':
            ar->currentline = (cf) ? get_line(vm, cf) : -1;
            break;
        case 'n':
            ar->namewhat = (cf) ? get_func_name(vm, cf, &ar->name) : nullptr;
            if (ar->namewhat == nullptr) {
                ar->namewhat = ""; // Not found.
                ar->name     = nullptr;
            }
            break;
        default:
            lulu_panicf("Invalid debug option '%c'.", *it);
            status = false;
            break;
        }
    }


    return status;
}

LULU_API int
lulu_get_info(lulu_VM *vm, const char *options, lulu_Debug *ar)
{
    lulu_assert(ar->_cf_index != 0);
    Call_Frame *cf = small_array_get_ptr(&vm->frames, ar->_cf_index);
    get_info(vm, options, ar, cf->function, cf);
    return 1;
}

LULU_API int
lulu_get_stack(lulu_VM *vm, int level, lulu_Debug *ar)
{
    const Call_Frame *cf = vm->caller;
    const Call_Frame *base = raw_data(vm->frames.data);
    int counter = level;
    for (; counter > 0 && cf > base; cf--) {
        counter--;
    }

    // Level found?
    if (counter == 0 && cf > base) {
        ar->_cf_index = cast(int)(cf - base);
        return 1;
    }
    return 0;
}
