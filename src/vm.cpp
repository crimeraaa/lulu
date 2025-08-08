#include <stdlib.h> // abort
#include <stdio.h>  // fprintf, stderr

#include "vm.hpp"
#include "debug.hpp"
#include "parser.hpp"

[[noreturn]]
static void
overflow_error(lulu_VM *vm, isize n, isize limit, const char *what)
{
    char buf[128];
    sprintf(buf, "%" ISIZE_FMT " / %" ISIZE_FMT, n, limit);
    vm_runtime_error(vm, "C stack overflow (%s %s used)", buf, what);
}

//=== CALL FRAME ARRAY MANIPULATION ==================================== {{{

static Call_Frame *
frame_get(lulu_VM *vm, isize i)
{
    return small_array_get_ptr(&vm->frames, i);
}

static void
frame_resize(lulu_VM *vm, isize i)
{
    small_array_resize(&vm->frames, i);
}

static Slice<Call_Frame>
frame_slice(lulu_VM *vm)
{
    return small_array_slice(vm->frames);
}

// Get the absolute index of `cf` in the `vm->frames` array.
static isize
frame_index(lulu_VM *vm, Call_Frame *cf)
{
    return ptr_index(frame_slice(vm), cf);
}

static void
frame_push(lulu_VM *vm, Closure *fn, Slice<Value> window, int to_return)
{
    isize n = small_array_len(vm->frames);
    if (n >= small_array_cap(vm->frames)) {
        overflow_error(vm, n, small_array_cap(vm->frames), "call frames");
    }
    small_array_resize(&vm->frames, n + 1);

    // Caller state
    Call_Frame *cf = frame_get(vm, n);
    cf->function  = fn;
    cf->window    = window;
    cf->saved_ip  = nullptr;
    cf->to_return = to_return;

    // VM state
    vm->caller = cf;
    vm->window = window;
}

static Call_Frame *
frame_pop(lulu_VM *vm)
{
    // Have a previous frame to return to?
    small_array_pop(&vm->frames);
    Call_Frame *frame = nullptr;
    isize i = small_array_len(vm->frames);
    if (i > 0) {
        frame      = frame_get(vm, i - 1);
        vm->window = frame->window;
    }
    vm->caller = frame;
    return frame;
}

//=== }}} ==================================================================

isize
vm_absindex(lulu_VM *vm, const Value *v)
{
    return ptr_index(vm->stack, v);
}

Value *
vm_base_ptr(lulu_VM *vm)
{
    return raw_data(vm->window);
}

Value *
vm_top_ptr(lulu_VM *vm)
{
    return raw_data(vm->window) + len(vm->window);
}

isize
vm_base_absindex(lulu_VM *vm)
{
    return ptr_index(vm->stack, vm_base_ptr(vm));
}

isize
vm_top_absindex(lulu_VM *vm)
{
    return ptr_index(vm->stack, vm_top_ptr(vm));
}

Builder *
vm_get_builder(lulu_VM *vm)
{
    Builder *b = &vm->builder;
    builder_reset(b);
    return b;
}

static void
set_error(lulu_VM *vm, isize old_cf, isize old_base, isize old_top)
{
    // TODO(2025-06-30): Check if `LULU_ERROR_MEMORY` works properly here
    Value v = vm_pop(vm);
    vm->caller = frame_get(vm, old_cf);
    vm->window = slice(vm->stack, old_base, old_top);
    frame_resize(vm, old_cf + 1);
    vm_push(vm, v);
}

Error
vm_run_protected(lulu_VM *vm, Protected_Fn fn, void *user_ptr)
{
    Error_Handler next{vm->error_handler, LULU_OK};
    // Chain new handler
    vm->error_handler = &next;

    isize old_base = vm_base_absindex(vm);
    isize old_top  = vm_top_absindex(vm);
    // Don't use pointers because in the future, `frames` may be dynamic.
    isize old_cf   = frame_index(vm, vm->caller);

    try {
        fn(vm, user_ptr);
    } catch (Error e) {
        next.error = e;
        set_error(vm, old_cf, old_base, old_top);
    }

    // Restore old handler
    vm->error_handler = next.prev;
    return next.error;
}

void
vm_throw(lulu_VM *vm, Error e)
{
    if (vm->error_handler != nullptr) {
        throw e;
    } else if (vm->panic_fn != nullptr) {
        set_error(vm, /* old_cf */ 0, /* old_base */ 0, /* old_top */ 0);
        vm->error_handler = nullptr;
        vm->panic_fn(vm);
    }
    exit(EXIT_FAILURE);
}

bool
vm_to_number(const Value *v, Value *out)
{
    // Nothing to do?
    if (v->is_number()) {
        out->set_number(v->to_number());
        return true;
    }
    // Try to parse the string.
    else if (v->is_string()) {
        Number d = 0;
        if (lstring_to_number(v->to_lstring(), &d)) {
            out->set_number(d);
            return true;
        }
    }
    return false;
}

bool
vm_to_string(lulu_VM *vm, Value *in_out)
{
    if (in_out->is_string()) {
        return true;
    } else if (in_out->is_number()) {
        Number_Buffer buf;
        LString  ls = number_to_lstring(in_out->to_number(), buf);
        OString *os = ostring_new(vm, ls);
        // @todo(2025-07-21): Check GC!
        in_out->set_string(os);
        return true;
    }
    return false;
}

const char *
vm_push_string(lulu_VM *vm, const LString &s)
{
    OString *o = ostring_new(vm, s);
    vm_push(vm, Value::make_string(o));
    return o->data;
}

const char *
vm_push_fstring(lulu_VM *vm, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const char *msg = vm_push_vfstring(vm, fmt, args);
    va_end(args);
    return msg;
}

const char *
vm_push_vfstring(lulu_VM *vm, const char *fmt, va_list args)
{
    const LString s     = lstring_from_cstring(fmt);
    const char  *start  = raw_data(s);
    const char  *cursor = start;

    Builder *b = vm_get_builder(vm);

    bool spec = false;
    for (; cursor < end(s); cursor++) {
        if (*cursor == '%' && !spec) {
            builder_write_lstring(vm, b, slice_pointer(start, cursor));
            spec = true;
            continue;
        }

        if (!spec) {
            continue;
        }

        switch (*cursor) {
        case 'c':
            builder_write_char(vm, b, cast(char)va_arg(args, int));
            break;
        case 'd':
        case 'i':
            builder_write_int(vm, b, va_arg(args, int));
            break;
        case 'f':
            builder_write_number(vm, b, va_arg(args, Number));
            break;
        case 's': {
            const char *s = va_arg(args, char *);
            LString ls = (s == nullptr) ? "(null)"_s : lstring_from_cstring(s);
            builder_write_lstring(vm, b, ls);
            break;
        }
        case 'p':
            builder_write_pointer(vm, b, va_arg(args, void *));
            break;
        default:
            lulu_panicf("Unsupported format specifier '%c'", *cursor);
            lulu_unreachable();
            break;
        }
        start = cursor + 1;
        spec  = false;
    }
    builder_write_lstring(vm, b, slice_pointer(start, cursor));
    return vm_push_string(vm, builder_to_string(*b));
}

void
vm_runtime_error(lulu_VM *vm, const char *fmt, ...)
{
    Call_Frame *cf = vm->caller;
    if (cf->is_lua()) {
        const Chunk *p = cf->to_lua()->chunk;
        isize  pc   = ptr_index(p->code, vm->saved_ip) - 1;
        int    line = chunk_get_line(p, pc);
        vm_push_fstring(vm, "%s:%i: ", p->source->to_cstring(), line);
    } else {
        vm_push_string(vm, "[C]: "_s);
    }

    va_list args;
    va_start(args, fmt);
    vm_push_vfstring(vm, fmt, args);
    va_end(args);

    lulu_concat(vm, 2);

    vm_throw(vm, LULU_ERROR_RUNTIME);
}

struct LULU_PRIVATE Load_Data {
    LString source;
    Stream *stream;
    Builder builder;
};

static void
load(lulu_VM *vm, void *user_ptr)
{
    Load_Data *d      = reinterpret_cast<Load_Data *>(user_ptr);
    OString   *source = ostring_new(vm, d->source);

    Chunk   *p = parser_program(vm, source, d->stream, &d->builder);
    Closure *f = closure_lua_new(vm, p);
    vm_push(vm, Value::make_function(f));
}

Error
vm_load(lulu_VM *vm, const LString &source, Stream *z)
{
    Load_Data d{source, z, {}};
    builder_init(&d.builder);
    Error e = vm_run_protected(vm, load, &d);
    builder_destroy(vm, &d.builder);
    return e;
}

void
vm_push(lulu_VM *vm, const Value &v)
{
    isize i = vm->window.len++;
    vm->window[i] = v;
}

Value
vm_pop(lulu_VM *vm)
{
    // Length update must occur AFTER the indexing to not trip up bounds check.
    Value v = vm->window[len(vm->window) - 1];
    vm->window.len--;
    return v;
}

void
vm_check_stack(lulu_VM *vm, int n)
{
    isize stop = vm_top_absindex(vm) + cast_isize(n);
    if (stop >= count_of(vm->stack)) {
        overflow_error(vm, stop, count_of(vm->stack), "stack slots");
    }
}

static void
protect(lulu_VM *vm, const Instruction *ip)
{
    vm->saved_ip = ip;
}

void
vm_call(lulu_VM *vm, const Value *ra, int n_args, int n_rets)
{
    // Account for any changes in the stack made by unprotected main function
    // or C functions.
    Call_Frame *cf = vm->caller;
    if (cf != nullptr && cf->is_c()) {
        // Ensure both slices have the same underlying data.
        // If this fails, this means we did not manage the call frames properly.
        lulu_assert(raw_data(vm->window) == raw_data(cf->window));
        cf->window = vm->window;
    }

    // `vm_call_fini()` may adjust `vm->window` in a different way than wanted.
    isize base    = vm_base_absindex(vm);
    isize new_top = vm_absindex(vm, ra) + cast_isize(n_rets);

    Call_Type t = vm_call_init(vm, ra, n_args, n_rets);
    if (t == CALL_LUA) {
        vm_execute(vm, 1);
    }

    // If vararg, then we assume the call already set the correct window.
    // NOTE(2025-07-05): `fn` may be dangling at this point!
    if (n_rets != VARARG) {
        vm->window = slice(vm->stack, base, new_top);
    }
}

static Call_Type
call_init_c(lulu_VM *vm, Closure *f, isize f_index, int n_args, int n_rets)
{
    vm_check_stack(vm, LULU_STACK_MIN);

    // Calling function isn't included in the stack frame.
    isize base = f_index + 1;
    isize top;
    if (n_args == VARARG) {
        top = vm_top_absindex(vm);
    } else {
        top = base + cast_isize(n_args);
    }
    Slice<Value> window = slice(vm->stack, base, top);
    frame_push(vm, f, window, n_rets);

    n_rets = f->to_c()->callback(vm);
    Value *first_ret = (n_rets > 0)
        ? &vm->window[len(vm->window) - cast_isize(n_rets)]
        : &vm->stack[f_index];

    vm_call_fini(vm, slice_pointer_len(first_ret, n_rets));
    return CALL_C;

}

static Call_Type
call_init_lua(lulu_VM *vm, Closure *fn, isize fn_index, int n_args, int n_rets)
{
    // Calling function isn't included in the stack frame.
    isize  base = fn_index + 1;
    Chunk *p    = fn->to_lua()->chunk;
    isize  top  = base + cast_isize(p->stack_used);

    vm_check_stack(vm, cast_int(top - base));
    Slice<Value> window = slice(vm->stack, base, top);

    // Some parameters weren't provided so they need to be initialized to nil?
    int extra = p->n_params;
    if (n_args == VARARG) {
        extra -= len(window);
    } else {
        extra -= n_args;
    }

    isize start_nil = base + p->n_params;
    if (extra > 0) {
        start_nil -= extra;
    }
    fill(slice(vm->stack, start_nil, top), nil);

    // We will goto `re_entry` in `vm_execute()`.
    vm->saved_ip = raw_data(p->code);
    frame_push(vm, fn, window, n_rets);
    return CALL_LUA;
}

Call_Type
vm_call_init(lulu_VM *vm, const Value *ra, int n_args, int n_rets)
{
    if (!ra->is_function()) {
        debug_type_error(vm, "call", ra);
    }

    // Inform previous caller of last execution point (even if caller is
    // a C function. When errors are thrown, saved_ip is always valid.
    if (vm->caller != nullptr) {
        vm->caller->saved_ip = vm->saved_ip;
    }

    Closure *fn       = ra->to_function();
    isize    fn_index = ptr_index(vm->stack, ra);
    // Can call directly?
    if (fn->is_c()) {
        return call_init_c(vm, fn, fn_index, n_args, n_rets);
    }
    return call_init_lua(vm, fn, fn_index, n_args, n_rets);
}

void
vm_call_fini(lulu_VM *vm, const Slice<Value> &results)
{
    Call_Frame *cf = vm->caller;
    bool vararg_return = (cf->to_return == VARARG);

    // Move results to the right place- overwrites calling function object.
    Slice<Value> dst{vm_base_ptr(vm) - 1, len(results)};
    copy(dst, results);

    int n_extra = cf->to_return - len(results);
    if (!vararg_return && n_extra > 0) {
        // Need to extend `dst` so that it also sees the extra values.
        dst.len += cast_isize(n_extra);

        // Remaining return values are initialized to nil, e.g. in assignments.
        fill(slice_from(dst, len(results)), nil);
    }

    cf = frame_pop(vm);

    // In an unprotected call, so no previous stack frame to restore.
    // This allows the `lulu_call()` API to work properly in such cases.
    if (cf == nullptr) {
        vm->window = dst;
        return;
    }

    if (vararg_return) {
        // Adjust VM's stack window so that it includes the last vararg.
        // We need to revert this change as soon as we can so that further
        // function calls see the full stack.
        vm->window = slice_pointer(raw_data(vm->window), end(dst));
    }

    // We will re-enter `vm_execute()`.
    vm->saved_ip = cf->saved_ip;
}

bool
vm_table_get(lulu_VM *vm, const Value *t, const Value &k, Value *out)
{
    if (t->is_table()) {
        // `table_get()` works under the assumption `k` is non-`nil`.
        if (k.is_nil()) {
            *out = nil;
            return false;
        }

        // do a primitive get (`rawget`)
        // @todo(2025-07-20): Check `v` is `nil` and lookup `index` metamethod
        Value v;
        bool  ok = table_get(t->to_table(), k, &v);
        *out = v;
        return ok;
    }
    debug_type_error(vm, "index", t);
    return false;
}

void
vm_table_set(lulu_VM *vm, const Value *t, const Value *k, const Value &v)
{
    if (t->is_table()) {
        // `table_set` assumes that we never use `nil` as a key.
        if (k->is_nil()) {
            debug_type_error(vm, "set index using", k);
        }
        table_set(vm, t->to_table(), *k, v);
        return;
    }
    debug_type_error(vm, "set index of", t);
}

enum Metamethod {
    MT_ADD, MT_SUB, MT_MUL, MT_DIV, MT_MOD, MT_POW, MT_UNM, // Arithmetic
    MT_LT, MT_LEQ, // Comparison
};

static void
arith(lulu_VM *vm, Metamethod mt, Value *ra, const Value *rkb, const Value *rkc)
{
    Value tmp_b, tmp_c;
    if (vm_to_number(rkb, &tmp_b) && vm_to_number(rkc, &tmp_c)) {
        Number x = tmp_b.to_number();
        Number y = tmp_c.to_number();
        Number n;
        switch (mt) {
        case MT_ADD: n = lulu_Number_add(x, y); break;
        case MT_SUB: n = lulu_Number_sub(x, y); break;
        case MT_MUL: n = lulu_Number_mul(x, y); break;
        case MT_DIV: n = lulu_Number_div(x, y); break;
        case MT_MOD: n = lulu_Number_mod(x, y); break;
        case MT_POW: n = lulu_Number_pow(x, y); break;
        case MT_UNM: n = lulu_Number_unm(x);    break;
        default:
            lulu_panicf("Invalid Metamethod(%i)", mt);
            lulu_unreachable();
            break;
        }
        ra->set_number(n);
        return;
    }
    debug_arith_error(vm, rkb, rkc);
}

static void
compare(lulu_VM *vm, Metamethod mt, Value *ra, const Value *rkb, const Value *rkc)
{
    Value tmp_b, tmp_c;
    if (vm_to_number(rkb, &tmp_b) && vm_to_number(rkc, &tmp_c)) {
        Number x = tmp_b.to_number();
        Number y = tmp_c.to_number();
        bool b;
        switch (mt) {
        case MT_LT:  b = lulu_Number_lt(x, y);  break;
        case MT_LEQ: b = lulu_Number_leq(x, y); break;
        default:
            lulu_panicf("Invalid Metamethod(%i)", mt);
            lulu_unreachable();
            break;
        }
        ra->set_boolean(b);
        return;
    }
    debug_compare_error(vm, rkb, rkc);
}

void
vm_execute(lulu_VM *vm, int n_calls)
{
    const Chunk       *chunk;
    const Instruction *ip;
    Slice<const Value> constants;
    Slice<Value>       window;

    // Restore state for Lua function calls and returns.
    re_entry:

    chunk     = vm->caller->to_lua()->chunk;
    ip        = vm->saved_ip;
    constants = slice_const(chunk->constants);
    window    = vm->window;

#define R(i)    window[i]
#define K(i)    constants[i]
#define KBX(i)  K(i.bx())
#define RK(i)   (Instruction::reg_is_k(i) ? K(Instruction::reg_get_k(i)) : R(i))

#define RA(i)   R(i.a())
#define RB(i)   R(i.b())
#define RC(i)   R(i.c())
#define RKB(i)  RK(i.b())
#define RKC(i)  RK(i.c())


#define BINARY_OP(number_fn, on_error_fn, metamethod, result_fn) {             \
    const Value *rb = &RKB(inst), *rc = &RKC(inst);                            \
    if (rb->is_number() && rc->is_number()) {                                  \
        result_fn(number_fn(rb->to_number(), rc->to_number()));                \
    } else {                                                                   \
        protect(vm, ip);                                                       \
        on_error_fn(vm, metamethod, ra, rb, rc);                               \
    }                                                                          \
}

#define DO_JUMP(offset)                                                        \
{                                                                              \
    ip += (offset);                                                            \
}

#define ARITH_RESULT(n)     ra->set_number(n)
#define ARITH_OP(fn, mt)    BINARY_OP(fn, arith, mt, ARITH_RESULT)

#define COMPARE_RESULT(b)                                                      \
{                                                                              \
    if (b == cast(bool)inst.a()) {                                             \
        DO_JUMP(ip->sbx())                                                     \
    }                                                                          \
    ip++;                                                                      \
}

#define COMPARE_OP(fn, mt)  BINARY_OP(fn, compare, mt, COMPARE_RESULT)

#ifdef LULU_DEBUG_TRACE_EXEC
    int pad = debug_get_pad(chunk);
#endif // LULU_DEBUG_TRACE_EXEC

    for (;;) {
        Instruction inst = *ip++;
        Value      *ra   = &RA(inst);

#ifdef LULU_DEBUG_TRACE_EXEC
        // We already incremented `ip`, so subtract 1 to get the original.
        isize pc = cast_isize(ptr_index(chunk->code, ip)) - 1;

        for (isize reg = 0, n = len(window); reg < n; reg++) {
            printf("\t[%" ISIZE_FMT "]\t", reg);
            value_print(window[reg]);

            const char *ident = chunk_get_local(chunk, cast_int(reg + 1), pc);
            if (ident != nullptr) {
                printf(" ; local %s", ident);
            }
            printf("\n");
        }
        printf("\n");
        debug_disassemble_at(chunk, inst, pc, pad);
#endif // LULU_DEBUG_TRACE_EXEC

        OpCode op = inst.op();
        switch (op) {
        case OP_MOVE:
            *ra = RB(inst);
            break;
        case OP_CONSTANT:
            *ra = KBX(inst);
            break;
        case OP_NIL:
            fill(slice_pointer(ra, &RB(inst) + 1), nil);
            break;
        case OP_BOOL:
            ra->set_boolean(cast(bool)inst.b());
            if (cast(bool)inst.c()) {
                ip++;
            }
            break;
        case OP_GET_GLOBAL: {
            Value k = KBX(inst);
            Value v;
            if (!table_get(vm->globals.to_table(), k, &v)) {
                const char *s = k.to_cstring();
                protect(vm, ip);
                vm_runtime_error(vm, "Attempt to read undefined variable '%s'", s);
            }
            *ra = v;
            break;
        }
        case OP_SET_GLOBAL: {
            Value k = KBX(inst);
            protect(vm, ip);
            table_set(vm, vm->globals.to_table(), k, *ra);
            break;
        }
        case OP_NEW_TABLE: {
            isize n_hash  = floating_byte_decode(inst.b());
            isize n_array = floating_byte_decode(inst.c());
            Table *t = table_new(vm, n_hash, n_array);
            ra->set_table(t);
            break;
        }
        case OP_GET_TABLE: {
            const Value *t = &RB(inst);
            const Value *k = &RKC(inst);
            protect(vm, ip);
            vm_table_get(vm, t, *k, ra);
            break;
        }
        case OP_SET_TABLE: {
            const Value *k = &RKB(inst);
            const Value *v = &RKC(inst);
            protect(vm, ip);
            vm_table_set(vm, ra, k, *v);
            break;
        }
        case OP_SET_ARRAY: {
            isize n      = cast_isize(inst.b());
            isize offset = cast_isize(inst.c()) * FIELDS_PER_FLUSH;

            if (n == VARARG) {
                // Number of arguments from R(A) up to top.
                n = len(vm->window) - inst.a() - 1;
            }

            // Guaranteed to be valid because this only occurs in table
            // contructors.
            Table *t = ra->to_table();
            for (isize i = 1; i <= n; i++) {
                table_set_integer(vm, t, offset + i, *(ra + i));
            }
            break;
        }
        case OP_ADD: ARITH_OP(lulu_Number_add, MT_ADD); break;
        case OP_SUB: ARITH_OP(lulu_Number_sub, MT_SUB); break;
        case OP_MUL: ARITH_OP(lulu_Number_mul, MT_MUL); break;
        case OP_DIV: ARITH_OP(lulu_Number_div, MT_DIV); break;
        case OP_MOD: ARITH_OP(lulu_Number_mod, MT_MOD); break;
        case OP_POW: ARITH_OP(lulu_Number_pow, MT_POW); break;
        case OP_EQ: {
            Value left  = RKB(inst);
            Value right = RKC(inst);

            protect(vm, ip);
            if ((left == right) == cast(bool)inst.a()) {
                lulu_assert(ip->op() == OP_JUMP);
                DO_JUMP(ip->sbx());
            }
            ip++;
            break;
        }
        case OP_LT:  COMPARE_OP(lulu_Number_lt,  MT_LT); break;
        case OP_LEQ: COMPARE_OP(lulu_Number_leq, MT_LEQ); break;
        case OP_UNM: {
            Value *rb = &RB(inst);
            if (rb->is_number()) {
                ra->set_number(lulu_Number_unm(rb->to_number()));
            } else {
                protect(vm, ip);
                arith(vm, MT_UNM, ra, rb, rb);
            }
            break;
        }
        case OP_NOT:
            ra->set_boolean(RB(inst).is_falsy());
            break;
        case OP_LEN:
            switch (ra->type()) {
            case VALUE_STRING:
                ra->set_number(cast_number(ra->to_ostring()->len));
                break;
            case VALUE_TABLE:
                ra->set_number(cast_number(table_len(ra->to_table())));
                break;
            default:
                protect(vm, ip);
                debug_type_error(vm, "get length of", ra);
                break;
            }
            break;
        case OP_CONCAT:
            protect(vm, ip);
            vm_concat(vm, ra, slice_pointer(&RB(inst), &RC(inst) + 1));
            break;
        case OP_TEST: {
            bool cond = cast(bool)inst.c();
            bool test = (!ra->is_falsy() == cond);

            // Ensure the next instruction is a jump before actually performing
            // it or skipping it.
            lulu_assert(ip->op() == OP_JUMP);
            if (test) {
                DO_JUMP(ip->sbx());
            }

            // If `DO_JUMP()` wasn't called then `ip` still points to `OP_JUMP`,
            // so increment to skip over it.
            ip++;
            break;
        }
        case OP_TEST_SET: {
            bool  cond = cast(bool)inst.c();
            Value rb   = RB(inst);
            bool  test = (!rb.is_falsy() == cond);
            lulu_assert(ip->op() == OP_JUMP);
            if (test) {
                *ra = rb;
                DO_JUMP(ip->sbx());
            }
            ip++;
            break;
        }
        case OP_JUMP:
            DO_JUMP(inst.sbx());
            break;
        case OP_FOR_PREP: {
            Value *index = ra;
            Value *limit = index + 1;
            Value *incr  = index + 2;

            protect(vm, ip);
            if (!vm_to_number(index, index)) {
                vm_runtime_error(vm, "'for' initial value must be a number");
            }
            if (!vm_to_number(limit, limit)) {
                vm_runtime_error(vm, "'for' limit must be a number");
            }
            if (!vm_to_number(incr, incr)) {
                vm_runtime_error(vm, "'for' increment must be a number");
            }

            // @todo(2025-07-28) Should we detect increment of 0?
            lulu_Number next = lulu_Number_sub(index->to_number(), incr->to_number());
            index->set_number(next);
            DO_JUMP(inst.sbx());
            break;
        }
        case OP_FOR_LOOP: {
            lulu_Number index = ra->to_number();
            lulu_Number limit = (ra + 1)->to_number();
            lulu_Number incr  = (ra + 2)->to_number();
            lulu_Number next  = lulu_Number_add(index, incr);

            // How we check `limit` depends if it's negative or not.
            if (lulu_Number_lt(0, incr) // 0 < incr => incr > 0
                ? lulu_Number_leq(next, limit) // incr >  0 => next <= limit
                : lulu_Number_leq(limit, next) // incr <= 0 => next >= limit
            ) {
                DO_JUMP(inst.sbx());
                // Update internal index.
                ra->set_number(next);

                // Then update external index.
                (ra + 3)->set_number(next);
            }
            break;
        }
        case OP_FOR_IN_LOOP: {
            Value *call_base = ra + 3;

            // Prepare call so that its registers can be overridden.
            call_base[2] = ra[2]; // internal control variable
            call_base[1] = ra[1]; // invariant state variable
            call_base[0] = ra[0]; // generator function

            // Registers for generator function, invariant state and index.
            isize top  = ptr_index(vm->window, call_base + 3);
            vm->window = slice_until(vm->window, top);

            // Number of user-facing variables to set.
            u16 n_vars = inst.c();
            protect(vm, ip);
            vm_call(vm, call_base, 2, cast_int(n_vars));

            // Previous call may reallocate stack.
            window    = vm->caller->window;
            call_base = &RA(inst) + 3;

            // Continue loop?
            if (!call_base->is_nil()) {
                // Save internal control variable.
                call_base[-1] = call_base[0];
                DO_JUMP(ip->sbx());
            }
            ip++;
            break;
        }
        case OP_CALL: {
            int n_args = cast_int(inst.b());
            int n_rets = cast_int(inst.c());
            protect(vm, ip);

            Call_Type t = vm_call_init(vm, ra, n_args, n_rets);
            if (t == CALL_LUA) {
                n_calls++;
                printf("=== BEGIN CALL ===\n");
                goto re_entry;
            }
            break;
        }
        case OP_RETURN: {
            int n_rets = cast_int(inst.b());
            if (n_rets == VARARG) {
                n_rets = cast_int(len(vm->window));
            }

            vm_call_fini(vm, slice_pointer_len(ra, n_rets));
            n_calls--;
            if (n_calls == 0) {
                return;
            }
            printf("\n=== END CALL ===\n\n");
            goto re_entry;
        }
        default:
            lulu_panicf("Invalid OpCode(%i)", op);
            lulu_unreachable();
        }
    }
}

void
vm_concat(lulu_VM *vm, Value *ra, Slice<Value> args)
{
    Builder *b = vm_get_builder(vm);
    for (Value &s : args) {
        if (!vm_to_string(vm, &s)) {
            debug_type_error(vm, "concatentate", &s);
        }
        builder_write_lstring(vm, b, s.to_lstring());
    }
    OString *os = ostring_new(vm, builder_to_string(*b));
    ra->set_string(os);
}
