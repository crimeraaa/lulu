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
    fprintf(stderr, "[FATAL]: Unprotected call to lulu API\n");
    throw e;
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
            slot = 0.0;
        }

        vm_execute(vm);
    }, &data);

    chunk_destroy(vm, data.chunk);
    return e;
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

#define ARITH_OP(fn)                                                           \
{                                                                              \
    u16 _b = getarg_b(i);                                                      \
    u16 _c = getarg_c(i);                                                      \
    Value rb = GET_RK(_b);                                                     \
    Value rc = GET_RK(_c);                                                     \
    ra = fn(rb, rc);                                                           \
}

#ifdef LULU_DEBUG_TRACE_EXEC
    int pad = debug_get_pad(chunk);
#endif // LULU_DEBUG_TRACE_EXEC

    for (;;) {
        Instruction i  = *ip++;
        Value      &ra =  window[getarg_a(i)];

#ifdef LULU_DEBUG_TRACE_EXEC
        for (size_t ii = 0, end = len(window); ii < end; ii++) {
            printf("\t[%zu]\t", ii);
            value_print(window[ii]);
            printf("\n");
        }
        printf("\n");
        debug_disassemble_at(chunk, i, cast_int(ip - raw_data(chunk.code) - 1), pad);
#endif // LULU_DEBUG_TRACE_EXEC

        switch (getarg_op(i)) {
        case OP_CONSTANT:
            ra = chunk.constants[getarg_bx(i)];
            break;
        case OP_UNM:
            ra = lulu_Number_unm(window[getarg_b(i)]);
            break;
        case OP_ADD: ARITH_OP(lulu_Number_add); break;
        case OP_SUB: ARITH_OP(lulu_Number_sub); break;
        case OP_MUL: ARITH_OP(lulu_Number_mul); break;
        case OP_DIV: ARITH_OP(lulu_Number_div); break;
        case OP_MOD: ARITH_OP(lulu_Number_mod); break;
        case OP_POW: ARITH_OP(lulu_Number_pow); break;
        case OP_RETURN: {
            /**
             * @note 2025-06-16
             *  Assumptions:
             *  1.) The stack was resized properly beforehand, so that doing
             *      pointer arithmetic is still within bounds even if we do not
             *      explicitly check.
             */
            Slice<Value> args = slice_make(&ra, getarg_b(i));
            for (auto v : args) {
                value_print(v);
                printf("\t");
            }
            printf("\n");
            return;
        }
        default:
            return;
        }
    }
}
