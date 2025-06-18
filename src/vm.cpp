#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include "vm.hpp"
#include "debug.hpp"
#include "parser.hpp"

void
vm_init(lulu_VM &vm, lulu_Allocator allocator, void *allocator_data)
{
    vm.allocator      = allocator;
    vm.allocator_data = allocator_data;
    vm.chunk          = nullptr;
    vm.saved_ip       = nullptr;
    builder_init(vm.builder);
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
        // What the hell?
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
    const Chunk &c    = *vm.chunk;
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
    Chunk  chunk;
};

Error
vm_interpret(lulu_VM &vm, String source, String script)
{
    Exec_Data data{source, script, {}};
    chunk_init(data.chunk, source);

    Error e = vm_run_protected(vm, [](lulu_VM &vm, void *user_ptr) {
        Exec_Data &d = *cast(Exec_Data *, user_ptr);
        parser_program(vm, d.chunk, d.script);

        // Fixed-size stack cannot accomodate this chunk?
        if (cast(size_t, d.chunk.stack_used) >= count_of(vm.stack)) {
            vm_throw(vm, LULU_ERROR_MEMORY);
        }
        vm.chunk  = &d.chunk;
        vm.window = slice_make(vm.stack, cast(size_t, d.chunk.stack_used));

        for (auto &slot : vm.window) {
            slot = value_make();
        }

        vm_execute(vm);
    }, &data);

    chunk_destroy(vm, data.chunk);
    return e;
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

void
vm_execute(lulu_VM &vm)
{
    Chunk              chunk  = *vm.chunk;
    const Instruction *ip     = raw_data(chunk.code);
    Slice<Value>       window = vm.window;

#define GET_RK(rk)                                                             \
    reg_is_rk(rk)                                                              \
        ? chunk.constants[reg_get_k(rk)]                                       \
        : window[rk]

#define BINARY_OP(number_fn, error_fn)                                         \
{                                                                              \
    u16   b  = getarg_b(i);                                                    \
    u16   c  = getarg_c(i);                                                    \
    Value rb = GET_RK(b);                                                      \
    Value rc = GET_RK(c);                                                      \
    if (!value_is_number(rb) || !value_is_number(rc)) {                        \
        protect(vm, ip);                                                       \
        error_fn(vm, rb, rc);                                                  \
    }                                                                          \
    ra = value_make(number_fn(rb.number, rc.number));                          \
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
            for (auto &v : slice_make(&ra, &rb + 1)) {
                v = value_make();
            }
            break;
        }
        case OP_LOAD_BOOL:
            ra = value_make(cast(bool, getarg_b(i)));
            break;
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
            ra = value_make(value_eq(rb, rc));
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
            ra = value_make(lulu_Number_unm(rb.number));
            break;
        }
        case OP_NOT: {
            Value rb = window[getarg_b(i)];
            ra = value_make(value_is_falsy(rb));
            break;
        }
        case OP_RETURN: {
            /**
             * @note 2025-06-16
             *  Assumptions:
             *  1.) The stack was resized properly beforehand, so that doing
             *      pointer arithmetic is still within bounds even if we do not
             *      explicitly check.
             */
            size_t n = cast(size_t, getarg_b(i));
            for (auto v : slice_make(&ra, n)) {
                value_print(v);
                printf("\t");
            }
            printf("\n");
            return;
        }
        default:
            lulu_unreachable();
        }
    }
}
