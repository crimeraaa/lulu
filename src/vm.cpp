#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "vm.hpp"
#include "debug.hpp"
#include "parser.hpp"
#include "object.hpp"
#include "function.hpp"

static int
base_print(lulu_VM *vm, int argc)
{
    for (int i = 0; i < argc; i++) {
        if (i > 0) {
            fputc('\t', stdout);
        }
        Value v = vm->window[i];
        value_print(v);
    }
    fputc('\n', stdout);
    return 0;
}

static int
base_clock(lulu_VM *vm, int)
{
    Number n = Number(clock()) / Number(CLOCKS_PER_SEC);
    vm_push(*vm, Value(n));
    return 1;
}

void
vm_init(lulu_VM &vm, lulu_Allocator allocator, void *allocator_data)
{
    vm.allocator      = allocator;
    vm.allocator_data = allocator_data;
    vm.saved_ip       = nullptr;
    vm.objects        = nullptr;
    vm.n_frames       = 0;
    builder_init(vm.builder);
    intern_init(vm.intern);
    table_init(vm.globals);

    // Ensure when we start interning strings we can already index.
    intern_resize(vm, vm.intern, 32);

    // Register the `print` function
    Function *f = function_new(vm, base_print);
    OString  *s = ostring_new(vm, "print"_s);
    table_set(vm, vm.globals, Value(s), Value(f));

    f = function_new(vm, base_clock);
    s = ostring_new(vm, "clock"_s);
    table_set(vm, vm.globals, Value(s), Value(f));
}

Builder &
vm_get_builder(lulu_VM &vm)
{
    Builder &b = vm.builder;
    builder_reset(b);
    return b;
}

void
vm_destroy(lulu_VM &vm)
{
    builder_destroy(vm, vm.builder);
    intern_destroy(vm, vm.intern);
    slice_delete(vm, vm.globals.entries);

    Object *o = vm.objects;
    while (o != nullptr) {
        // Save becase `o` is about to be invalidated.
        Object *next = o->base.next;
        object_free(vm, o);
        o = next;
    }
}

Error
vm_run_protected(lulu_VM &vm, Protected_Fn fn, void *user_ptr)
{
    Error_Handler next{vm.error_handler, LULU_OK};
    // Chain new handler
    vm.error_handler = &next;

    try {
        fn(vm, user_ptr);
    } catch (Error e) {
        lulu_assert(e != LULU_OK);
        next.error = e;
    }

    // Restore old handler
    vm.error_handler = next.prev;
    return next.error;
}

void
vm_throw(lulu_VM &vm, Error e)
{
    if (vm.error_handler != nullptr) {
        throw e;
    }
    // Don't throw an unhandle-able exception, we may be in a C application!
    fprintf(stderr, "[FATAL]: Unprotected call to lulu API\n");
    abort();
}

void
vm_syntax_error(lulu_VM &vm, String file, int line, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stdout, STRING_FMTSPEC ":%i: ", string_fmtarg(file), line);
    vfprintf(stdout, fmt, args);
    fputc('\n', stdout);
    va_end(args);
    vm_throw(vm, LULU_ERROR_SYNTAX);
}

void
vm_runtime_error(lulu_VM &vm, const char *act, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const Chunk &c    = *vm.caller->function->l.chunk;
    const int    pc   = cast_int(vm.saved_ip - raw_data(c.code) - 1);
    const int    line = chunk_get_line(c, pc);

    fprintf(stdout, STRING_FMTSPEC ":%i: Attempt to %s ",
        string_fmtarg(c.source), line, act);
    vfprintf(stdout, fmt, args);
    fputc('\n', stdout);
    va_end(args);
    vm_throw(vm, LULU_ERROR_RUNTIME);
}

struct Exec_Data {
    String source, script;
};

Error
vm_interpret(lulu_VM &vm, String source, String script)
{
    Exec_Data d{source, script};
    Error e = vm_run_protected(vm, [](lulu_VM &vm, void *user_ptr) {
        // Reset stack for consecutive runs.
        vm.window = Slice(vm.stack, 0, 0);

        Exec_Data &d = *cast(Exec_Data *)(user_ptr);
        Chunk     *c = parser_program(vm, d.source, d.script);
        Function  *f = function_new(vm, c);
        vm_push(vm, Value(f));
        vm_call(vm, vm.window[len(vm.window) - 1], 0, 0);
        vm_execute(vm);
    }, &d);
    return e;
}

void
vm_push(lulu_VM &vm, Value v)
{
    size_t i = vm.window.len++;
    vm.window[i] = v;
}

static Value *
vm_base_ptr(lulu_VM &vm)
{
    return raw_data(vm.window);
}

static Value *
vm_top_ptr(lulu_VM &vm)
{
    return raw_data(vm.window) + len(vm.window);
}

static size_t
vm_base_index(lulu_VM &vm)
{
    return ptr_index(vm.stack, vm_base_ptr(vm));
}

static size_t
vm_top_index(lulu_VM &vm)
{
    return ptr_index(vm.stack, vm_top_ptr(vm));
}

void
vm_check_stack(lulu_VM &vm, int n)
{
    size_t stop = vm_top_index(vm) + cast_size(n);
    if (stop >= count_of(vm.stack)) {
        vm_runtime_error(vm, "stack overflow", "%zu / %zu stack slots",
            stop, count_of(vm.stack));
    }
}

[[noreturn]]
static void
type_error(lulu_VM &vm, const char *act, const Value &v)
{
    vm_runtime_error(vm, act, "a %s value", value_type_name(v));
}

[[noreturn]]
static void
arith_error(lulu_VM &vm, const Value &a, const Value &b)
{
    const Value &v = value_is_number(a) ? b : a;
    type_error(vm, "perform arithmetic on", v);
}

[[noreturn]]
static void
compare_error(lulu_VM &vm, const Value &a, const Value &b)
{
    const Value &v = value_is_number(a) ? a : b;
    type_error(vm, "compare", v);
}

static void
protect(lulu_VM &vm, const Instruction *ip)
{
    vm.saved_ip = ip;
}

static void
vm_frame_push(lulu_VM &vm, Function *fn, Slice<Value> window, int expected_returned)
{
    if (cast_size(vm.n_frames) >= count_of(vm.frames)) {
        vm_runtime_error(vm, "stack overflow", "%i / %zu frames",
            vm.n_frames, count_of(vm.frames));
    }

    Call_Frame *cf = &vm.frames[vm.n_frames];
    cf->function           = fn;
    cf->window             = window;
    cf->expected_returned  = expected_returned;

    vm.n_frames++;
    vm.caller   = cf;
    vm.window   = window;

    if (function_is_lua(fn)) {
        for (Value &slot : Slice(window, fn->l.n_params, len(window))) {
            slot = Value();
        }
    }
}

static Call_Frame *
vm_frame_pop(lulu_VM &vm)
{
    vm.n_frames--;
    // Have a previous frame to return to?
    if (vm.n_frames > 0) {
        Call_Frame *f = &vm.frames[vm.n_frames - 1];
        vm.caller = f;
        vm.window = f->window;
        return f;
    }
    return nullptr;
}

Call_Type
vm_call(lulu_VM &vm, Value &ra, int argc, int expected_returned)
{
    if (!value_is_function(ra)) {
        type_error(vm, "call", ra);
    }

    Function *fn       = value_to_function(ra);
    size_t    ra_index = ptr_index(vm.stack, &ra);

    // Calling function isn't included in the stack frame.
    size_t base        = ra_index + 1;
    bool   vararg_call = (argc == VARARG);

    // Can call directly?
    if (function_is_c(fn)) {
        vm_check_stack(vm, LULU_STACK_MIN);

        size_t top;
        if (vararg_call) {
            top  = vm_top_index(vm);
            argc = cast_int(top - base);
        } else {
            top = base + cast_size(argc);
        }
        vm_frame_push(vm, fn, Slice(vm.stack, base, top), expected_returned);
        int actual_returned = fn->c.callback(&vm, argc);

        Value &first_ret = (actual_returned > 0)
            ? vm.window[len(vm.window) - cast_size(actual_returned)]
            : vm.stack[ra_index];
        vm_return(vm, first_ret, actual_returned);
        return CALL_C;
    }

    size_t top = base + cast_size(fn->l.chunk->stack_used);
    int extra = fn->l.n_params - ((vararg_call) ? cast_int(top) - cast_int(base) : argc);
    // Some parameters weren't provided so they need to be initialized to nil?
    if (extra > 0) {
        top += cast_size(extra);
    }
    // May invalidate `ra`.
    vm_check_stack(vm, cast_int(top) - cast_int(base));
    vm_frame_push(vm, fn, Slice(vm.stack, base, top), expected_returned);
    return CALL_LUA;
}

Call_Type
vm_return(lulu_VM &vm, Value &ra, int actual_returned)
{
    Call_Frame *frame = vm.caller;
    size_t base       = vm_base_index(vm);
    size_t ra_index   = ptr_index(vm.stack, &ra);
    bool   vararg_return = (frame->expected_returned == VARARG);

    // Overwrite stack slot of function object.
    base -= 1;
    Slice<Value> returned  = Slice(vm.stack, ra_index, ra_index + cast_size(actual_returned));
    Slice<Value> finalized = Slice(vm.stack, base,     base + cast_size(actual_returned));

    // Move results to the right place.
    for (size_t reg = 0, last = len(finalized); reg < last; reg++) {
        finalized[reg] = returned[reg];
    }

    int extra = (vararg_return) ? -1 : frame->expected_returned - actual_returned;

    if (extra > 0) {
        // Need to extend `finalized` so that it also sees the extra values.
        finalized.len += cast_size(extra);
        for (Value &slot : Slice(&finalized[actual_returned], end(finalized))) {
            slot = Value();
        }
    }

    frame = vm_frame_pop(vm);
    if (vararg_return) {
        // Adjust VM's stack window so that it includes the last vararg.
        // We need to revert this change as soon as we can so that
        size_t new_top = ptr_index(vm.stack, end(finalized));
        vm.window = Slice(vm.stack, base, new_top);
    }

    // Returning from main function, so no previous stack frame to restore.
    // This is also important to allow the `lulu_call()` API to work properly.
    if (frame == nullptr) {
        vm.caller = nullptr;
        vm.window = finalized;
        return CALL_C;
    }
    vm.caller = frame;
    return CALL_LUA;
}

void
vm_execute(lulu_VM &vm)
{
    Chunk              chunk  = *vm.caller->function->l.chunk;
    const Instruction *ip     = raw_data(chunk.code);
    Slice<Value>       window = vm.window;

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
        ra = Value(number_fn(value_to_number(rb), value_to_number(rc)));       \
    }                                                                          \
}

#define ARITH_OP(fn)    BINARY_OP(fn, arith_error)
#define COMPARE_OP(fn)  BINARY_OP(fn, compare_error)


#ifdef LULU_DEBUG_TRACE_EXEC
    int pad = debug_get_pad(chunk);
#endif // LULU_DEBUG_TRACE_EXEC

    for (;;) {
#ifdef LULU_DEBUG_TRACE_EXEC
        for (size_t ii = 0, end = len(window); ii < end; ii++) {
            printf("\t[%zu]\t", ii);
            value_print(window[ii]);
            printf("\n");
        }
        printf("\n");
        debug_disassemble_at(chunk, cast_int(ip - raw_data(chunk.code)), pad);
#endif // LULU_DEBUG_TRACE_EXEC

        Instruction i  = *ip++;
        Value      &ra =  window[getarg_a(i)];

        switch (getarg_op(i)) {
        case OP_CONSTANT:
            ra = chunk.constants[getarg_bx(i)];
            break;
        case OP_LOAD_NIL: {
            Value &rb = window[getarg_b(i)];
            for (Value &v : Slice(&ra, &rb + 1)) {
                v = Value();
            }
            break;
        }
        case OP_LOAD_BOOL:
            ra = Value(bool(getarg_b(i)));
            break;
        case OP_GET_GLOBAL: {
            Value k = chunk.constants[getarg_bx(i)];
            protect(vm, ip);

            Table_Result r = table_get(vm.globals, k);
            if (!r.ok) {
                vm_runtime_error(vm, "read undefined variable",
                    "'%s'", value_to_cstring(k));
            }
            ra = r.value;
            break;
        }
        case OP_SET_GLOBAL: {
            Value k = chunk.constants[getarg_bx(i)];
            protect(vm, ip);
            table_set(vm, vm.globals, k, ra);
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
            ra = Value(rb == rc);
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
            ra = Value(lulu_Number_unm(value_to_number(rb)));
            break;
        }
        case OP_NOT: {
            Value rb = window[getarg_b(i)];
            ra = Value(value_is_falsy(rb));
            break;
        }
        case OP_CONCAT: {
            Value &rb = window[getarg_b(i)];
            Value &rc = window[getarg_c(i)];
            protect(vm, ip);
            vm_concat(vm, ra, Slice(&rb, &rc + 1));
            break;
        }
        case OP_CALL: {
            protect(vm, ip);
            vm_call(vm, ra, cast_int(getarg_b(i)), cast_int(getarg_c(i)));
            break;
        }
        case OP_RETURN: {
            protect(vm, ip);
            vm_return(vm, ra, cast_int(getarg_b(i)));
            return;
        }
        default:
            lulu_unreachable();
        }
    }
}

void
vm_concat(lulu_VM &vm, Value &ra, Slice<Value> args)
{
    Builder &b = vm_get_builder(vm);
    for (const Value &s : args) {
        if (!value_is_string(s)) {
            type_error(vm, "concatentate", s);
        }
        builder_write_string(vm, b, value_to_string(s));
    }
    OString *o = ostring_new(vm, builder_to_string(b));
    ra = Value(o);
}
