#include <stdlib.h> // abort
#include <stdio.h>  // fprintf, stderr

#include "vm.hpp"
#include "debug.hpp"
#include "parser.hpp"

isize
vm_absindex(lulu_VM *vm, Value *v)
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

static void
required_allocations(lulu_VM *vm, void *)
{
    // Ensure when we start interning strings we can already index.
    intern_resize(vm, &vm->intern, 32);

    // TODO(2025-06-30): Mark the memory error string as "immortal"?
    OString *o = ostring_new(vm, lstring_from_cstring(LULU_MEMORY_ERROR_STRING));

    o = ostring_new(vm, "_G"_s);
    table_set(vm, &vm->globals, value_make_string(o), value_make_table(&vm->globals));
}

bool
vm_init(lulu_VM *vm, lulu_Allocator allocator, void *allocator_data)
{
    small_array_init(&vm->frames);

    vm->allocator      = allocator;
    vm->allocator_data = allocator_data;
    vm->saved_ip       = nullptr;
    vm->objects        = nullptr;
    vm->window         = slice_array(vm->stack, 0, 0);

    builder_init(&vm->builder);
    intern_init(&vm->intern);
    table_init(&vm->globals);
    Error e = vm_run_protected(vm, required_allocations, nullptr);
    return e == LULU_OK;
}

Builder *
vm_get_builder(lulu_VM *vm)
{
    Builder *b = &vm->builder;
    builder_reset(b);
    return b;
}

void
vm_destroy(lulu_VM *vm)
{
    builder_destroy(vm, &vm->builder);
    intern_destroy(vm, &vm->intern);
    slice_delete(vm, vm->globals.entries);

    Object *o = vm->objects;
    while (o != nullptr) {
        // Save because `o` is about to be invalidated.
        Object *next = o->base.next;
        object_free(vm, o);
        o = next;
    }
}

Error
vm_run_protected(lulu_VM *vm, Protected_Fn fn, void *user_ptr)
{
    Error_Handler next{vm->error_handler, LULU_OK};
    // Chain new handler
    vm->error_handler = &next;

    isize old_base     = vm_base_absindex(vm);
    isize old_top      = vm_top_absindex(vm);
    // Don't use pointers because in the future, `frames` may be dynamic.
    isize old_cf_index = ptr_index(vm->frames.data, vm->caller);

    try {
        fn(vm, user_ptr);
    } catch (Error e) {
        next.error = e;

        // TODO(2025-06-30): Check if `LULU_ERROR_MEMORY` works properly here
        Value msg  = vm_pop(vm);
        vm->window = slice_array(vm->stack, old_base, old_top);
        vm->caller = small_array_get_ptr(&vm->frames, old_cf_index);
        small_array_resize(&vm->frames, old_cf_index + 1);
        vm_push(vm, msg);
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
    }
    // Don't throw an unhandle-able exception, we may be in a C application!
    fprintf(stderr, "[FATAL]: Unprotected call to lulu API\n");
    abort();
}

const char *
vm_push_string(lulu_VM *vm, LString s)
{
    OString *o = ostring_new(vm, s);
    vm_push(vm, value_make_string(o));
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
            lulu_assertf(false, "Unsupported format specifier '%c'", *cursor);
            lulu_unreachable();
            break;
        }
        start = cursor + 1;
        spec  = false;
    }
    builder_write_lstring(vm, b, slice_pointer(start, cursor));
    return vm_push_string(vm, builder_to_string(b));
}

void
vm_syntax_error(lulu_VM *vm, LString source, int line, const char *fmt, ...)
{
    va_list args;
    vm_push_string(vm, source);
    vm_push_fstring(vm, ":%i: ", line);

    va_start(args, fmt);
    vm_push_vfstring(vm, fmt, args);
    va_end(args);

    lulu_concat(vm, 3);

    vm_throw(vm, LULU_ERROR_SYNTAX);
}

void
vm_runtime_error(lulu_VM *vm, const char *act, const char *fmt, ...)
{
    va_list args;
    Call_Frame *cf = vm->caller;
    Closure    *f  = cf->function;
    if (closure_is_lua(f)) {
        Chunk *c    = f->l.chunk;
        isize  pc   = ptr_index(c->code, vm->saved_ip) - 1;
        int    line = chunk_get_line(c, pc);
        vm_push_string(vm, c->source);
        vm_push_fstring(vm, ":%i: ", line);
        lulu_concat(vm, 2);
    } else {
        vm_push_string(vm, "[C]: "_s);
    }
    vm_push_fstring(vm, "Attempt to %s ", act);

    va_start(args, fmt);
    vm_push_vfstring(vm, fmt, args);
    va_end(args);

    lulu_concat(vm, 3);

    vm_throw(vm, LULU_ERROR_RUNTIME);
}

struct Load_Data {
    LString source, script;
    Builder builder;
};

static void
load(lulu_VM *vm, void *user_ptr)
{
    Load_Data *d = cast(Load_Data *)(user_ptr);
    Chunk     *c = parser_program(vm, d->source, d->script, &d->builder);
    Closure   *f = closure_new(vm, c);
    vm_push(vm, value_make_function(f));
}

Error
vm_load(lulu_VM *vm, LString source, LString script)
{
    Load_Data d{source, script, {}};
    builder_init(&d.builder);
    Error e = vm_run_protected(vm, load, &d);
    builder_destroy(vm, &d.builder);
    return e;
}

void
vm_push(lulu_VM *vm, Value v)
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
        vm_runtime_error(vm, "stack overflow",
            "%" ISIZE_FMTSPEC " / %" ISIZE_FMTSPEC " stack slots",
            stop, count_of(vm->stack));
    }
}

[[noreturn]]
static void
type_error(lulu_VM *vm, const char *act, const Value &v)
{
    vm_runtime_error(vm, act, "a %s value", value_type_name(v));
}

[[noreturn]]
static void
arith_error(lulu_VM *vm, const Value &a, const Value &b)
{
    const Value &v = value_is_number(a) ? b : a;
    type_error(vm, "perform arithmetic on", v);
}

[[noreturn]]
static void
compare_error(lulu_VM *vm, const Value &a, const Value &b)
{
    const Value &v = value_is_number(a) ? a : b;
    type_error(vm, "compare", v);
}

static void
protect(lulu_VM *vm, const Instruction *ip)
{
    vm->saved_ip = ip;
}

static void
vm_frame_push(lulu_VM *vm, Closure *fn, Slice<Value> window, int expected_returned)
{
    // Before transferring control to the new caller, inform the previous
    // caller of where we last left off.
    if (vm->caller != nullptr && closure_is_lua(vm->caller->function)) {
        vm->caller->saved_ip = vm->saved_ip;
    }

    Call_Frame cf;
    cf.function           = fn;
    cf.window             = window;
    cf.saved_ip           = nullptr;
    cf.expected_returned  = expected_returned;
    small_array_push(&vm->frames, cf);

    vm->caller = small_array_get_ptr(&vm->frames, small_array_len(vm->frames) - 1);
    vm->window = window;

    if (closure_is_lua(fn)) {
        // Initialize the stack frame (sans paramters) to all nil.
        Slice<Value> dst = slice_slice(window, cast_isize(fn->l.n_params), len(window));
        fill(dst, nil);
    }
}

static Call_Frame *
vm_frame_pop(lulu_VM *vm)
{
    // Have a previous frame to return to?
    small_array_pop(&vm->frames);
    Call_Frame *frame = nullptr;
    if (small_array_len(vm->frames) > 0) {
        frame      = small_array_get_ptr(&vm->frames, small_array_len(vm->frames) - 1);
        vm->window = frame->window;
    }
    vm->caller = frame;
    return frame;
}

void
vm_call(lulu_VM *vm, Value &ra, int n_args, int n_rets)
{
    // Account for any changes in the stack made by unprotected main function
    // or C functions.
    Call_Frame *caller = vm->caller;
    if (caller != nullptr) {
        // Ensure both slices have the same underlying data.
        // If this fails, this means we did not manage the call frames properly.
        lulu_assert(raw_data(vm->window) == raw_data(caller->window));
        caller->window = vm->window;
    }

    // `vm_call_fini()` may adjust `vm->window` in a different way than wanted.
    isize base    = vm_base_absindex(vm);
    isize new_top = vm_absindex(vm, &ra) + cast_isize(n_rets);

    Call_Type t = vm_call_init(vm, ra, n_args, n_rets);
    if (t == CALL_LUA) {
        vm_execute(vm, 1);
    }

    // If vararg, then we assume the call already set the correct window.
    // NOTE(2025-07-05): `fn` may be dangling at this point!
    if (n_rets != VARARG) {
        vm->window = slice_array(vm->stack, base, new_top);
    }
}

Call_Type
vm_call_init(lulu_VM *vm, Value &ra, int argc, int expected_returned)
{
    if (!value_is_function(ra)) {
        type_error(vm, "call", ra);
    }

    Closure *fn       = value_to_function(ra);
    isize    fn_index = ptr_index(vm->stack, &ra);

    // Calling function isn't included in the stack frame.
    isize base        = fn_index + 1;
    bool  vararg_call = (argc == VARARG);

    // Can call directly?
    if (closure_is_c(fn)) {
        vm_check_stack(vm, LULU_STACK_MIN);

        isize top;
        if (vararg_call) {
            top  = vm_top_absindex(vm);
            argc = cast_int(top - base);
        } else {
            top = base + cast_isize(argc);
        }
        vm_frame_push(vm, fn, slice_array(vm->stack, base, top), expected_returned);
        int actual_returned = fn->c.callback(vm, argc);

        Value &first_ret = (actual_returned > 0)
            ? vm->window[len(vm->window) - cast_isize(actual_returned)]
            : vm->stack[fn_index];
        vm_call_fini(vm, first_ret, actual_returned);
        return CALL_C;
    }

    isize top = base + cast_isize(fn->l.chunk->stack_used);
    int extra = fn->l.n_params - ((vararg_call) ? cast_int(top - base) : argc);
    // Some parameters weren't provided so they need to be initialized to nil?
    if (extra > 0) {
        top += cast_isize(extra);
    }
    // May invalidate `ra`.
    vm_check_stack(vm, cast_int(top - base));
    vm_frame_push(vm, fn, slice_array(vm->stack, base, top), expected_returned);
    return CALL_LUA;
}

Call_Type
vm_call_fini(lulu_VM *vm, Value &ra, int actual_returned)
{
    Call_Frame *frame = vm->caller;
    bool vararg_return = (frame->expected_returned == VARARG);

    // Move results to the right place- overwrites calling function object.
    Slice<Value> results{vm_base_ptr(vm) - 1, cast_isize(actual_returned)};
    Slice<Value> tmp{&ra, cast_isize(actual_returned)};
    copy(results, tmp);

    int extra = frame->expected_returned - actual_returned;
    if (!vararg_return && extra > 0) {
        // Need to extend `results` so that it also sees the extra values.
        results.len += cast_isize(extra);

        // Remaining return values are initialized to nil, e.g. in assignments.
        Slice<Value> remaining = slice_pointer(&results[actual_returned], end(results));
        fill(remaining, nil);
    }

    frame = vm_frame_pop(vm);

    // In an unprotected call, so no previous stack frame to restore.
    // This allows the `lulu_call()` API to work properly in such cases.
    if (frame == nullptr) {
        vm->window = results;
        return CALL_C;
    }

    if (vararg_return) {
        // Adjust VM's stack window so that it includes the last vararg.
        // We need to revert this change as soon as we can so that further
        // function calls see the full stack.
        vm->window = slice_pointer(raw_data(vm->window), end(results));
    }
    return CALL_LUA;
}

void
vm_execute(lulu_VM *vm, int n_calls)
{
    // Copy by value for speed.
    Chunk              chunk  = *vm->caller->function->l.chunk;
    const Instruction *ip     = raw_data(chunk.code);
    Slice<Value>       window = vm->window;

#define GET_RK(rk)                                                             \
    reg_is_rk(rk)                                                              \
        ? chunk.constants[reg_get_k(rk)]                                       \
        : window[rk]

#define BINARY_OP(number_fn, error_fn)                                         \
{                                                                              \
    u16          b  = getarg_b(i);                                             \
    u16          c  = getarg_c(i);                                             \
    const Value &rb = GET_RK(b);                                               \
    const Value &rc = GET_RK(c);                                               \
    if (!value_is_number(rb) || !value_is_number(rc)) {                        \
        protect(vm, ip);                                                       \
        error_fn(vm, rb, rc);                                                  \
    } else {                                                                   \
        ra = number_fn(value_to_number(rb), value_to_number(rc));              \
    }                                                                          \
}

#define ARITH_OP(fn)    BINARY_OP(fn, arith_error)
#define COMPARE_OP(fn)  BINARY_OP(fn, compare_error)


#ifdef LULU_DEBUG_TRACE_EXEC
    int pad = debug_get_pad(&chunk);
#endif // LULU_DEBUG_TRACE_EXEC

    for (;;) {
        Instruction i  = *ip++;
        Value      &ra =  window[getarg_a(i)];

#ifdef LULU_DEBUG_TRACE_EXEC
        // We already incremented `ip`, so subtract 1 to get the original.
        isize pc = cast_isize(ptr_index(chunk.code, ip)) - 1;

        for (isize reg = 0, end = len(window); reg < end; reg++) {
            printf("\t[%" ISIZE_FMTSPEC "]\t", reg);
            value_print(window[reg]);

            const char *id = chunk_get_local(&chunk, cast_int(reg + 1), pc);
            if (id != nullptr) {
                printf(" ; local %s", id);
            }
            printf("\n");
        }
        printf("\n");
        debug_disassemble_at(&chunk, i, pc, pad);
#endif // LULU_DEBUG_TRACE_EXEC

        switch (getarg_op(i)) {
        case OP_CONSTANT:
            ra = chunk.constants[getarg_bx(i)];
            break;
        case OP_LOAD_NIL: {
            Value &rb = window[getarg_b(i)];
            Slice<Value> dst = slice_pointer(&ra, &rb + 1);
            fill(dst, nil);
            break;
        }
        case OP_LOAD_BOOL:
            ra = cast(bool)getarg_b(i);
            break;
        case OP_GET_GLOBAL: {
            Value k = chunk.constants[getarg_bx(i)];
            Value v;
            protect(vm, ip);
            if (!table_get(&vm->globals, k, &v)) {
                const char *s = value_to_cstring(k);
                vm_runtime_error(vm, "read undefined variable", "'%s'", s);
            }
            ra = v;
            break;
        }
        case OP_SET_GLOBAL: {
            Value k = chunk.constants[getarg_bx(i)];
            protect(vm, ip);
            table_set(vm, &vm->globals, k, ra);
            break;
        }
        case OP_NEW_TABLE: {
            isize n_hash  = cast_isize(getarg_b(i));
            isize n_array = cast_isize(getarg_c(i));
            Table *t = table_new(vm, n_hash, n_array);
            ra = value_make_table(t);
            break;
        }
        case OP_GET_TABLE: {
            Value &t = window[getarg_b(i)];
            Value &k = GET_RK(getarg_c(i));
            protect(vm, ip);
            if (!value_is_table(t)) {
                type_error(vm, "index", t);
            } else if (value_is_nil(k)) {
                // t[nil] is not stored, but return `nil` anyway.
                ra = nil;
            } else {
                table_get(value_to_table(t), k, &ra);
            }
            break;
        }
        case OP_SET_TABLE: {
            Value &k = GET_RK(getarg_b(i));
            Value &v = GET_RK(getarg_c(i));
            protect(vm, ip);
            if (!value_is_table(ra)) {
                type_error(vm, "index", ra);
            } else if (value_is_nil(k)) {
                type_error(vm, "index", k);
            }
            table_set(vm, value_to_table(ra), k, v);
            break;
        }
        case OP_MOVE: {
            Value &rb = window[getarg_b(i)];
            ra = rb;
            break;
        }
        case OP_ADD: ARITH_OP(lulu_Number_add); break;
        case OP_SUB: ARITH_OP(lulu_Number_sub); break;
        case OP_MUL: ARITH_OP(lulu_Number_mul); break;
        case OP_DIV: ARITH_OP(lulu_Number_div); break;
        case OP_MOD: ARITH_OP(lulu_Number_mod); break;
        case OP_POW: ARITH_OP(lulu_Number_pow); break;
        case OP_EQ: {
            u16   b  = getarg_b(i);
            u16   c  = getarg_c(i);
            Value rb = GET_RK(b);
            Value rc = GET_RK(c);
            ra = (rb == rc);
            break;
        }
        case OP_LT:  COMPARE_OP(lulu_Number_lt); break;
        case OP_LEQ: COMPARE_OP(lulu_Number_leq); break;
        case OP_UNM: {
            Value &rb = window[getarg_b(i)];
            if (!value_is_number(rb)) {
                protect(vm, ip);
                arith_error(vm, rb, rb);
            }
            ra = lulu_Number_unm(value_to_number(rb));
            break;
        }
        case OP_NOT: {
            Value rb = window[getarg_b(i)];
            ra = value_is_falsy(rb);
            break;
        }
        case OP_CONCAT: {
            Value &rb = window[getarg_b(i)];
            Value &rc = window[getarg_c(i)];
            protect(vm, ip);
            vm_concat(vm, ra, slice_pointer(&rb, &rc + 1));
            break;
        }
        case OP_TEST: {
            bool c    = cast(bool)getarg_c(i);
            bool test = (!value_is_falsy(ra) == c);
            // If test fails, skip the next instruction (assuming it's a jump)
            if (!test) {
                lulu_assert(getarg_op(*ip) == OP_JUMP);
                ip++;
            }
            break;
        }
        case OP_JUMP: {
            i32 offset = getarg_sbx(i);
            ip += offset;
            break;
        }
        case OP_CALL: {
            int argc              = cast_int(getarg_b(i));
            int expected_returned = cast_int(getarg_c(i));
            protect(vm, ip);

            Call_Type t = vm_call_init(vm, ra, argc, expected_returned);
            if (t == CALL_LUA) {
                n_calls++;
            }
            break;
        }
        case OP_RETURN: {
            int actual_returned = cast_int(getarg_b(i));
            protect(vm, ip);
            vm_call_fini(vm, ra, actual_returned);
            n_calls--;
            if (n_calls == 0) {
                return;
            }
            break;
        }
        default:
            lulu_unreachable();
        }
    }
}

void
vm_concat(lulu_VM *vm, Value &ra, Slice<Value> args)
{
    Builder *b = vm_get_builder(vm);
    for (const Value &s : args) {
        if (!value_is_string(s)) {
            type_error(vm, "concatentate", s);
        }
        builder_write_lstring(vm, b, value_to_lstring(s));
    }
    OString *o = ostring_new(vm, builder_to_string(b));
    ra = value_make_string(o);
}
