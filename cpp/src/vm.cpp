#include <stdio.h>  // fprintf, stderr
#include <stdlib.h> // abort
#include <string.h> // strchr

#include "debug.hpp"
#include "parser.hpp"
#include "vm.hpp"

[[noreturn]] static void
overflow_error(lulu_VM *L, isize n, isize limit, const char *what)
{
    char buf[128];
    sprintf(buf, "%" ISIZE_FMT " / %" ISIZE_FMT, n, limit);
    vm_runtime_error(L, "C stack overflow (%s %s used)", buf, what);
}

static void
required_allocations(lulu_VM *L, void *)
{
    Table *t = table_new(L, /*n_hash=*/8, /*n_array=*/0);
    L->globals.set_table(t);
    // Ensure when we start interning strings we can already index.
    intern_resize(L, &G(L)->intern, 32);

    OString *o = ostring_new(L, lstring_literal(LULU_MEMORY_ERROR_STRING));
    o->set_fixed();
    lexer_global_init(L);
}

struct LG {
    lulu_Global G;
    lulu_VM L;
};

LULU_API lulu_VM *
lulu_open(lulu_Allocator allocator, void *allocator_data)
{
    static LG lg;
    lulu_Global *g = &lg.G;
    lulu_VM *L = &lg.L;

    // Global state
    *g = {};
    g->allocator = allocator;
    g->allocator_data = allocator_data;
    // VM state
    *L = {};
    L->G = g;
    // @note(2025-08-30) Point to stack already so length updates are valid.
    L->window = slice(L->stack, 0, 0);

    // 'pause' GC
    g->gc_threshold = USIZE_MAX;
    Error e = vm_run_protected(L, required_allocations, nullptr);
    // Prepare GC for actual work
    g->gc_threshold = GC_THRESHOLD_INIT;
    g->gc_prev_threshold = g->gc_threshold;
    if (e != LULU_OK) {
        lulu_close(L);
        return nullptr;
    }
    return L;
}

LULU_API void
lulu_close(lulu_VM *L)
{
    lulu_Global *g = G(L);
    builder_destroy(L, &g->builder);
    intern_destroy(L, &g->intern);

    // Free ALL objects unconditionally since the VM is about to be freed.
    Object *o = g->objects;
    while (o != nullptr) {
        // Save because `o` is about to be invalidated.
        Object *next = o->next();
        object_free(L, o);
        o = next;
    }
}

//=== CALL FRAME ARRAY MANIPULATION ==================================== {{{

static Call_Frame *
frame_get(lulu_VM *L, isize i)
{
    return small_array_get_ptr(&L->frames, i);
}

static void
frame_resize(lulu_VM *L, isize i)
{
    small_array_resize(&L->frames, i);
}

static Slice<Call_Frame>
frame_slice(lulu_VM *L)
{
    return small_array_slice(L->frames);
}

// Get the absolute index of `cf` in the `L->frames` array.
static int
frame_index(lulu_VM *L, Call_Frame *cf)
{
    if (cf == nullptr) {
        return 0;
    }
    return ptr_index(frame_slice(L), cf);
}

static void
frame_push(lulu_VM *L, Closure *fn, Slice<Value> window, int to_return)
{
    isize n = small_array_len(L->frames);
    if (n >= small_array_cap(L->frames)) {
        overflow_error(L, n, small_array_cap(L->frames), "call frames");
    }
    small_array_resize(&L->frames, n + 1);

    // Caller state
    Call_Frame *cf = frame_get(L, n);
    cf->function   = fn;
    cf->window     = window;
    cf->saved_ip   = nullptr;
    cf->to_return  = to_return;

    // VM state
    L->caller = cf;
    L->window = window;
}

static Call_Frame *
frame_pop(lulu_VM *L)
{
    // Have a previous frame to return to?
    small_array_pop(&L->frames);
    Call_Frame *frame = nullptr;
    isize       i     = small_array_len(L->frames);
    if (i > 0) {
        frame      = frame_get(L, i - 1);
        L->window = frame->window;
    }
    L->caller = frame;
    return frame;
}

//=== }}} ==================================================================

int
vm_absindex(lulu_VM *L, const Value *v)
{
    return ptr_index(L->stack, v);
}

Value *
vm_base_ptr(lulu_VM *L)
{
    return raw_data(L->window);
}

Value *
vm_top_ptr(lulu_VM *L)
{
    return raw_data(L->window) + len(L->window);
}

int
vm_base_absindex(lulu_VM *L)
{
    return ptr_index(L->stack, vm_base_ptr(L));
}

int
vm_top_absindex(lulu_VM *L)
{
    return ptr_index(L->stack, vm_top_ptr(L));
}

Builder *
vm_get_builder(lulu_VM *L)
{
    Builder *b = &G(L)->builder;
    builder_reset(b);
    return b;
}

static OString *
get_error_object(lulu_VM *L, Error e)
{
    switch (e) {
    case LULU_OK:
        break;
    case LULU_ERROR_MEMORY:
        return ostring_new(L, lstring_literal(LULU_MEMORY_ERROR_STRING));
    case LULU_ERROR_RUNTIME:
    case LULU_ERROR_SYNTAX:
        return L->window[len(L->window) - 1].to_ostring();
    default:
        break;
    }
    lulu_unreachable();
    return nullptr;
}

static void
set_error_object(lulu_VM *L, Error e, int old_cf, int old_base, int old_top)
{
    // Close pending closures
    // We assume that stack is at least LULU_STACK_MIN, so old_top is valid.
    upvalue_close(L, &L->stack[old_top]);
    OString *s = get_error_object(L, e);
    L->caller = frame_get(L, old_cf);
    frame_resize(L, old_cf + 1);

    // Put AFTER in case above calls GC
    L->stack[old_top].set_string(s);
    L->window = slice(L->stack, old_base, old_top + 1);
}

Error
vm_pcall(lulu_VM *L, Protected_Fn fn, void *user_ptr)
{
    int old_base = vm_base_absindex(L);
    int old_top  = vm_top_absindex(L);
    // Don't use pointers because in the future, `frames` may be dynamic.
    int old_cf = frame_index(L, L->caller);

    Error e = vm_run_protected(L, fn, user_ptr);
    if (e != LULU_OK) {
        set_error_object(L, e, old_cf, old_base, old_top);
    }
    return e;
}

Error
vm_run_protected(lulu_VM *L, Protected_Fn fn, void *user_ptr)
{
    Error_Handler next{L->error_handler, LULU_OK};
    // Chain new handler
    L->error_handler = &next;
    try {
        fn(L, user_ptr);
    } catch (Error e) {
        next.error = e;
    }

    // Restore old handler
    L->error_handler = next.prev;
    return next.error;
}

void
vm_throw(lulu_VM *L, Error e)
{
    lulu_Global *g = G(L);
    if (L->error_handler != nullptr) {
        throw e;
    } else if (g->panic_fn != nullptr) {
        set_error_object(L, e, /*old_cf=*/0, /*old_base=*/0, /*old_top=*/0);
        L->error_handler = nullptr;
        g->panic_fn(L);
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
    else if (v->is_string())
    {
        Number d = 0;
        if (lstring_to_number(v->to_lstring(), &d)) {
            out->set_number(d);
            return true;
        }
    }
    return false;
}

bool
vm_to_string(lulu_VM *L, Value *in_out)
{
    if (in_out->is_string()) {
        return true;
    } else if (in_out->is_number()) {
        Number_Buffer buf;
        LString       ls = number_to_lstring(in_out->to_number(), slice(buf));
        OString      *os = ostring_new(L, ls);
        // @todo(2025-07-21): Check GC!
        in_out->set_string(os);
        return true;
    }
    return false;
}

const char *
vm_push_string(lulu_VM *L, LString s)
{
    OString *o = ostring_new(L, s);
    vm_push_value(L, Value::make_string(o));
    return o->data;
}

const char *
vm_push_fstring(lulu_VM *L, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const char *msg = vm_push_vfstring(L, fmt, args);
    va_end(args);
    return msg;
}

const char *
vm_push_vfstring(lulu_VM *L, const char *fmt, va_list args)
{
    Builder    *b      = vm_get_builder(L);
    const char *cursor = fmt;

    for (;;) {
        // Points to '%' if possible.
        const char *arg = strchr(cursor, '%');
        if (arg == nullptr) {
            // Write remaining non-specifier sequence, if any exists.
            builder_write_lstring(L, b, lstring_from_cstring(cursor));
            break;
        }
        // Write non-specifier sequence before this point, if any exists.
        // Empty on first iteration. May be non-empty on subsequent iterations.
        builder_write_lstring(L, b, slice_pointer(cursor, arg));

        // Poke at character after '%'.
        char spec = *(arg + 1);
        switch (spec) {
        case 'c':
            builder_write_char(L, b, va_arg(args, int));
            break;
        case 'd':
        case 'i':
            builder_write_int(L, b, va_arg(args, int));
            break;
        case 'f':
            builder_write_number(L, b, va_arg(args, Number));
            break;
        case 's': {
            const char *s = va_arg(args, char *);
            LString s2 = (s == nullptr) ? "(null)"_s : lstring_from_cstring(s);
            builder_write_lstring(L, b, s2);
            break;
        }
        case 'p':
            builder_write_pointer(L, b, va_arg(args, void *));
            break;
        default:
            lulu_panicf("Unsupported format specifier '%c'", spec);
            break;
        }
        // Point to format string after this format specifier sequence.
        cursor = arg + 2;
    }
    return vm_push_string(L, builder_to_string(*b));
}

void
vm_runtime_error(lulu_VM *L, const char *fmt, ...)
{
    Call_Frame *cf = L->caller;
    if (cf->is_lua()) {
        const Chunk *p    = cf->to_lua()->chunk;
        int          pc   = ptr_index(p->code, L->saved_ip) - 1;
        int          line = chunk_line_get(p, pc);
        vm_push_fstring(L, "%s:%i: ", p->source->to_cstring(), line);
    } else {
        vm_push_string(L, "[C]: "_s);
    }

    va_list args;
    va_start(args, fmt);
    vm_push_vfstring(L, fmt, args);
    va_end(args);

    lulu_concat(L, 2);

    vm_throw(L, LULU_ERROR_RUNTIME);
}

struct Load_Data {
    LString source;
    Stream *stream;
    Builder builder;
};


// Analogous to `ldo.c:f_parser()` in Lua 5.1.5.
static void
load(lulu_VM *L, void *user_ptr)
{
    Load_Data *d      = reinterpret_cast<Load_Data *>(user_ptr);
    OString   *source = ostring_new(L, d->source);

    // We need to do this as the string is otherwise not reachable. Lua gets
    // around this by not checking GC inside of its malloc wrapper, but rather
    // only checking GC in the macro `luaC_checkGC()` which is only called
    // at certain points, which by then this string is already reachable via
    // its parent `Chunk *`.
    vm_push_value(L, Value::make_string(source));

    // luaC_checkGC(L);
    Chunk   *p = parser_program(L, source, d->stream, &d->builder);
    Closure *f = closure_lua_new(L, p);
    vm_pop_value(L);
    vm_push_value(L, Value::make_function(f));
}

Error
vm_load(lulu_VM *L, LString source, Stream *z)
{
    Load_Data d{source, z, {}};
    Error e = vm_pcall(L, load, &d);
    builder_destroy(L, &d.builder);
    return e;
}

void
vm_check_stack(lulu_VM *L, int n)
{
    int stop = vm_top_absindex(L) + n;
    if (stop >= count_of(L->stack)) {
        overflow_error(L, stop, count_of(L->stack), "stack slots");
    }
}

// Must be called before functions that could potentially throw errors.
// This makes it so that they can properly disassemble the culprit instruction.
static void
protect(lulu_VM *L, const Instruction *ip)
{
    L->saved_ip = ip;
}

void
vm_call(lulu_VM *L, const Value *ra, int n_args, int n_rets)
{
    // Account for any changes in the stack made by unprotected main function
    // or C functions.
    Call_Frame *cf = L->caller;
    if (cf != nullptr && cf->is_c()) {
        // Ensure both slices have the same underlying data.
        // If this fails, this means we did not manage the call frames properly.
        lulu_assert(raw_data(L->window) == raw_data(cf->window));
        cf->window = L->window;
    }

    // `vm_call_fini()` may adjust `L->window` in a different way than wanted.
    int base    = vm_base_absindex(L);
    int new_top = vm_absindex(L, ra) + n_rets;

    Call_Type t = vm_call_init(L, ra, n_args, n_rets);
    if (t == CALL_LUA) {
        vm_execute(L, 1);
    }

    // If vararg, then we assume the call already set the correct window.
    // NOTE(2025-07-05): `fn` may be dangling at this point!
    if (n_rets != VARARG) {
        L->window = slice(L->stack, base, new_top);
    }
    // luaC_checkGC(L);
}

static Call_Type
call_init_c(lulu_VM *L, Closure *f, int f_index, int n_args, int n_rets)
{
    vm_check_stack(L, LULU_STACK_MIN);

    // Calling function isn't included in the stack frame.
    int base = f_index + 1;
    int top;
    if (n_args == VARARG) {
        top = vm_top_absindex(L);
    } else {
        top = base + n_args;
    }
    Slice<Value> window = slice(L->stack, base, top);
    frame_push(L, f, window, n_rets);

    n_rets           = f->to_c()->callback(L);
    Value *first_ret = (n_rets > 0) ? &L->window[len(L->window) - n_rets]
                                    : &L->stack[f_index];

    vm_call_fini(L, slice_pointer_len(first_ret, n_rets));
    return CALL_C;
}

static Call_Type
call_init_lua(lulu_VM *L, Closure *fn, int fn_index, int n_args, int n_rets)
{
    // Calling function isn't included in the stack frame.
    int    base = fn_index + 1;
    Chunk *p    = fn->to_lua()->chunk;
    int    top  = base + p->stack_used;

    vm_check_stack(L, top - base);
    Slice<Value> window = slice(L->stack, base, top);

    // Some parameters weren't provided so they need to be initialized to nil?
    int extra = p->n_params;
    if (n_args == VARARG) {
        extra -= len(window);
    } else {
        extra -= n_args;
    }

    int start_nil = base + p->n_params;
    if (extra > 0) {
        start_nil -= extra;
    }
    fill(slice(L->stack, start_nil, top), nil);

    // We will goto `re_entry` in `vm_execute()`.
    L->saved_ip = raw_data(p->code);
    frame_push(L, fn, window, n_rets);
    return CALL_LUA;
}

Call_Type
vm_call_init(lulu_VM *L, const Value *ra, int n_args, int n_rets)
{
    if (!ra->is_function()) {
        debug_type_error(L, "call", ra);
    }

    // Inform previous caller of last execution point (even if caller is
    // a C function. When errors are thrown, saved_ip is always valid.
    if (L->caller != nullptr) {
        L->caller->saved_ip = L->saved_ip;
    }

    Closure *fn       = ra->to_function();
    int      fn_index = ptr_index(L->stack, ra);
    // Can call directly?
    if (fn->is_c()) {
        return call_init_c(L, fn, fn_index, n_args, n_rets);
    }
    return call_init_lua(L, fn, fn_index, n_args, n_rets);
}

void
vm_call_fini(lulu_VM *L, Slice<Value> results)
{
    Call_Frame *cf            = L->caller;
    bool        vararg_return = (cf->to_return == VARARG);

    // Move results to the right place- overwrites calling function object.
    Slice<Value> dst{vm_base_ptr(L) - 1, len(results)};
    copy(dst, results);

    int n_extra = cf->to_return - len(results);
    if (!vararg_return && n_extra > 0) {
        // Need to extend `dst` so that it also sees the extra values.
        dst.len += n_extra;

        // Remaining return values are initialized to nil, e.g. in assignments.
        fill(slice_from(dst, len(results)), nil);
    }

    cf = frame_pop(L);

    // In an unprotected call, so no previous stack frame to restore.
    // This allows the `lulu_call()` API to work properly in such cases.
    if (cf == nullptr) {
        L->window = dst;
        return;
    }

    if (vararg_return) {
        // Adjust VM's stack window so that it includes the last vararg.
        // We need to revert this change as soon as we can so that further
        // function calls see the full stack.
        L->window = slice_pointer(raw_data(L->window), end(dst));
    }

    // We will re-enter `vm_execute()`.
    L->saved_ip = cf->saved_ip;
}

bool
vm_table_get(lulu_VM *L, const Value *t, Value k, Value *out)
{
    if (t->is_table()) {
        // `table_get()` works under the assumption `k` is non-`nil`.
        if (k.is_nil()) {
            *out = nil;
            return false;
        }

        // do a primitive get (`rawget`)
        // @todo(2025-07-20): Check `v` is `nil` and lookup `index` metamethod
        bool key_exists;
        Value v = table_get(t->to_table(), k, &key_exists);
        *out = v;
        return key_exists;
    }
    debug_type_error(L, "index", t);
    return false;
}

void
vm_table_set(lulu_VM *L, const Value *t, const Value *k, Value v)
{
    if (t->is_table()) {
        // `table_set` assumes that we never use `nil` as a key.
        if (k->is_nil()) {
            debug_type_error(L, "set index using", k);
        }
        Value *tk = table_set(L, t->to_table(), *k);
        // luaC_barriert(L, t, v);
        *tk = v;
        return;
    }
    debug_type_error(L, "set index of", t);
}

enum Metamethod {
    MT_ADD, MT_SUB, MT_MUL, MT_DIV, MT_MOD, MT_POW, MT_UNM, // Arithmetic
    MT_LT, MT_LEQ, // Comparison
};

static void
arith(lulu_VM *L, Metamethod mt, Value *ra, const Value *rkb, const Value *rkc)
{
    Value tmp_b, tmp_c;
    if (vm_to_number(rkb, &tmp_b) && vm_to_number(rkc, &tmp_c)) {
        Number x = tmp_b.to_number();
        Number y = tmp_c.to_number();
        Number n;
        switch (mt) {
        case MT_ADD:
            n = lulu_Number_add(x, y);
            break;
        case MT_SUB:
            n = lulu_Number_sub(x, y);
            break;
        case MT_MUL:
            n = lulu_Number_mul(x, y);
            break;
        case MT_DIV:
            n = lulu_Number_div(x, y);
            break;
        case MT_MOD:
            n = lulu_Number_mod(x, y);
            break;
        case MT_POW:
            n = lulu_Number_pow(x, y);
            break;
        case MT_UNM:
            n = lulu_Number_unm(x);
            break;
        default:
            lulu_panicf("Invalid Metamethod(%i)", mt);
            break;
        }
        ra->set_number(n);
        return;
    }
    debug_arith_error(L, rkb, rkc);
}

static void
compare(lulu_VM *L, Metamethod mt, Value *ra, const Value *rkb,
    const Value *rkc)
{
    Value tmp_b, tmp_c;
    if (vm_to_number(rkb, &tmp_b) && vm_to_number(rkc, &tmp_c)) {
        Number x = tmp_b.to_number();
        Number y = tmp_c.to_number();
        bool   b;
        switch (mt) {
        case MT_LT:
            b = lulu_Number_lt(x, y);
            break;
        case MT_LEQ:
            b = lulu_Number_leq(x, y);
            break;
        default:
            lulu_panicf("Invalid Metamethod(%i)", mt);
            break;
        }
        ra->set_boolean(b);
        return;
    }
    debug_compare_error(L, rkb, rkc);
}

void
vm_execute(lulu_VM *L, int n_calls)
{
    const Closure_Lua *caller;
    const Chunk       *chunk;
    const Instruction *ip;
    Slice<const Value> constants;
    Slice<Value>       window;

// Restore state for Lua function calls and returns.
re_entry:

    caller    = L->caller->to_lua();
    chunk     = caller->chunk;
    ip        = L->saved_ip;
    constants = slice_const(chunk->constants);
    window    = L->window;

#define R(i)   window[i]
#define K(i)   constants[i]
#define KBX(i) K(i.bx())
#define RK(i)  (Instruction::reg_is_k(i) ? K(Instruction::reg_get_k(i)) : R(i))

#define RA(i)  R(i.a())
#define RB(i)  R(i.b())
#define RC(i)  R(i.c())
#define RKB(i) RK(i.b())
#define RKC(i) RK(i.c())


#define BINARY_OP(number_fn, on_error_fn, metamethod, result_fn)               \
    {                                                                          \
        const Value *rb = &RKB(inst), *rc = &RKC(inst);                        \
        if (rb->is_number() && rc->is_number()) {                              \
            result_fn(number_fn(rb->to_number(), rc->to_number()));            \
        } else {                                                               \
            protect(L, ip);                                                   \
            on_error_fn(L, metamethod, ra, rb, rc);                           \
        }                                                                      \
    }

#define DO_JUMP(offset)                                                        \
    {                                                                          \
        ip += (offset);                                                        \
    }

#define ARITH_RESULT(n)  ra->set_number(n)
#define ARITH_OP(fn, mt) BINARY_OP(fn, arith, mt, ARITH_RESULT)

#define COMPARE_RESULT(b)                                                      \
    {                                                                          \
        if (b == static_cast<bool>(inst.a())) {                                \
            DO_JUMP(ip->sbx())                                                 \
        }                                                                      \
        ip++;                                                                  \
    }

#define COMPARE_OP(fn, mt) BINARY_OP(fn, compare, mt, COMPARE_RESULT)

#ifdef LULU_DEBUG_TRACE_EXEC
    int pad = debug_get_pad(chunk);
#endif // LULU_DEBUG_TRACE_EXEC

    for (;;) {
        Instruction inst = *ip++;
        Value      *ra   = &RA(inst);

#ifdef LULU_DEBUG_TRACE_EXEC
        // We already incremented `ip`, so subtract 1 to get the original.
        int pc = ptr_index(chunk->code, ip) - 1;
        for (int reg = 0, n = static_cast<int>(len(window)); reg < n; reg++) {
            printf("\t[%i]\t", reg);
            value_print(window[reg]);

            const char *ident = chunk_get_local(chunk, reg + 1, pc);
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
            ra->set_boolean(static_cast<bool>(inst.b()));
            if (static_cast<bool>(inst.c())) {
                ip++;
            }
            break;
        case OP_GET_GLOBAL: {
            Value k = KBX(inst);
            Value v;
            if (!vm_table_get(L, &L->globals, k, &v)) {
                protect(L, ip);
                vm_runtime_error(L, "Attempt to read undefined variable '%s'",
                    k.to_cstring());
            }
            *ra = v;
            break;
        }
        case OP_SET_GLOBAL: {
            Value k = KBX(inst);
            protect(L, ip);
            vm_table_set(L, &L->globals, &k, *ra);
            break;
        }
        case OP_NEW_TABLE: {
            isize  n_hash  = floating_byte_decode(inst.b());
            isize  n_array = floating_byte_decode(inst.c());
            Table *t       = table_new(L, n_hash, n_array);
            ra->set_table(t);
            // Must occur AFTER setting `ra` so that the table is on the stack!
            // Protect(luaC_checkGC(L));
            break;
        }
        case OP_GET_TABLE: {
            const Value *t = &RB(inst);
            const Value *k = &RKC(inst);
            protect(L, ip);
            vm_table_get(L, t, *k, ra);
            break;
        }
        case OP_SET_TABLE: {
            const Value *k = &RKB(inst);
            const Value *v = &RKC(inst);
            protect(L, ip);
            vm_table_set(L, ra, k, *v);
            break;
        }
        case OP_SET_ARRAY: {
            isize n      = inst.b();
            isize offset = static_cast<isize>(inst.c()) * FIELDS_PER_FLUSH;

            if (n == VARARG) {
                // Number of arguments from R(A) up to top.
                n = len(L->window) - inst.a() - 1;
            }

            // Guaranteed to be valid because this only occurs in table
            // contructors.
            Table *t = ra->to_table();
            for (isize i = 1; i <= n; i++) {
                Value *v = table_set_integer(L, t, offset + i);
                *v = ra[i];
            }
            break;
        }
        case OP_GET_UPVALUE: {
            Upvalue *up = caller->upvalues[inst.b()];
            *ra = *up->value;
            break;
        }
        case OP_SET_UPVALUE: {
            Upvalue *up = caller->upvalues[inst.b()];
            *up->value = *ra;
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

            protect(L, ip);
            if ((left == right) == static_cast<bool>(inst.a())) {
                lulu_assert(ip->op() == OP_JUMP);
                DO_JUMP(ip->sbx());
            }
            ip++;
            break;
        }
        case OP_LT:
            COMPARE_OP(lulu_Number_lt, MT_LT);
            break;
        case OP_LEQ:
            COMPARE_OP(lulu_Number_leq, MT_LEQ);
            break;
        case OP_UNM: {
            Value *rb = &RB(inst);
            if (rb->is_number()) {
                ra->set_number(lulu_Number_unm(rb->to_number()));
            } else {
                protect(L, ip);
                arith(L, MT_UNM, ra, rb, rb);
            }
            break;
        }
        case OP_NOT:
            ra->set_boolean(RB(inst).is_falsy());
            break;
        case OP_LEN: {
            Value *rb = &window[inst.b()];
            switch (rb->type()) {
            case VALUE_STRING:
                ra->set_number(static_cast<Number>(rb->to_ostring()->len));
                break;
            case VALUE_TABLE:
                ra->set_number(static_cast<Number>(table_len(rb->to_table())));
                break;
            default:
                protect(L, ip);
                debug_type_error(L, "get length of", rb);
                break;
            }
            break;
        }
        case OP_CONCAT:
            protect(L, ip);
            vm_concat(L, ra, slice_pointer(&RB(inst), &RC(inst) + 1));
            // luaC_checkGC(L);
            break;
        case OP_TEST: {
            bool cond = inst.c();
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
            bool  cond = inst.c();
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
            Value *index = &ra[0];
            Value *limit = &ra[1];
            Value *incr  = &ra[2];

            protect(L, ip);
            if (!vm_to_number(index, index)) {
                vm_runtime_error(L, "'for' initial value must be a number");
            }
            if (!vm_to_number(limit, limit)) {
                vm_runtime_error(L, "'for' limit must be a number");
            }
            if (!vm_to_number(incr, incr)) {
                vm_runtime_error(L, "'for' increment must be a number");
            }

            // @todo(2025-07-28) Should we detect increment of 0?
            lulu_Number next =
                lulu_Number_sub(index->to_number(), incr->to_number());
            index->set_number(next);
            DO_JUMP(inst.sbx());
            break;
        }
        case OP_FOR_LOOP: {
            lulu_Number index = ra[0].to_number();
            lulu_Number limit = ra[1].to_number();
            lulu_Number incr  = ra[2].to_number();
            lulu_Number next  = lulu_Number_add(index, incr);

            // How we check `limit` depends if it's negative or not.
            if (lulu_Number_lt(0, incr)            // 0 < incr => incr > 0
                    ? lulu_Number_leq(next, limit) // incr >  0 => next <= limit
                    : lulu_Number_leq(limit, next) // incr <= 0 => next >= limit
            )
            {
                DO_JUMP(inst.sbx());
                // Update internal index.
                ra[0].set_number(next);

                // Then update external index.
                ra[3].set_number(next);
            }
            break;
        }
        case OP_FOR_IN: {
            Value *call_base = ra + 3;

            // Prepare call so that its registers can be overridden.
            call_base[0] = ra[0]; // generator function
            call_base[1] = ra[1]; // invariant state variable
            call_base[2] = ra[2]; // internal control variable

            // Registers for generator function, invariant state and index.
            int top    = ptr_index(window, call_base + 3);
            L->window = slice_until(window, top);

            // Number of user-facing variables to set.
            u16 n_vars = inst.c();
            protect(L, ip);

            /** @note(2025-09-01) May call another vm_execute(). */
            vm_call(L, call_base, 2, n_vars);

            /** @brief Account for `vm_call()` resizing/reallocating the stack.
             *
             * @note(2025-08-28)
             *  It is important to update BOTH local and global window, so that
             *  stack windows are consistent for garbage collection.
             */
            window    = L->caller->window;
            L->window = window;
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
            int n_args = inst.b();
            int n_rets = inst.c();
            protect(L, ip);

            // @note(2025-08-29) `vm->window` may have been changed by this!
            Call_Type t = vm_call_init(L, ra, n_args, n_rets);
            if (t == CALL_LUA) {
                n_calls++;
#if LULU_DEBUG_TRACE_EXEC
                printf("=== BEGIN CALL ===\n");
#endif // LULU_DEBUG_TRACE_EXEC
                // Local `window` will be re-assigned properly anyway.
                goto re_entry;
            }
            /** @brief Need to fix local `window` because it may be dangling
             *  otherwise. This is mainly an issue for variadic calls.
             *
             * @note(2025-08-27) Concept check: tests/factorial.lua
             */
            window = L->window;
            break;
        }
        case OP_CLOSURE: {
            Closure *f = closure_lua_new(L, chunk->children[inst.bx()]);
            // Ensure closure lives on the stack already to avoid collection.
            // This also ensures the upvalues are not collected.
            ra->set_function(f);
            for (Upvalue *&up : f->to_lua()->slice_upvalues()) {
                // Just need to copy someone else's upvalues?
                if (ip->op() == OP_GET_UPVALUE) {
                    up = caller->upvalues[ip->b()];
                }
                // We're the first ones to manage this upvalue.
                else {
                    lulu_assertf(ip->op() == OP_MOVE,
                        "Invalid upvalue opcode '%s'", opnames[ip->op()]);
                    // We haven't transferred control to the closure, so our
                    // local indices are still valid.
                    Value *v = &window[ip->b()];
                    up = upvalue_find(L, v);
                }
                ip++;
            }
            // luaC_checkGC(L);
            break;
        }
        case OP_CLOSE:
            upvalue_close(L, ra);
            break;
        case OP_RETURN: {
            int n_rets = inst.b();
            if (n_rets == VARARG) {
                n_rets = len(L->window);
            }

            if (L->open_upvalues != nullptr) {
                upvalue_close(L, &window[0]);
            }

            vm_call_fini(L, slice_pointer_len(ra, n_rets));
            n_calls--;
            if (n_calls == 0) {
                return;
            }
#if LULU_DEBUG_TRACE_EXEC
            printf("\n=== END CALL ===\n\n");
#endif // LULU_DEBUG_TRACE_EXEC
            goto re_entry;
        }
        default:
            lulu_panicf("Invalid OpCode(%i)", op);
        }
    }
}

void
vm_concat(lulu_VM *L, Value *ra, Slice<Value> args)
{
    Builder *b = vm_get_builder(L);
    for (Value &s : args) {
        if (!vm_to_string(L, &s)) {
            debug_type_error(L, "concatentate", &s);
        }
        builder_write_lstring(L, b, s.to_lstring());
    }
    OString *os = ostring_new(L, builder_to_string(*b));
    ra->set_string(os);
}
