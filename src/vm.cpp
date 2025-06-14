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
    abort();
}

void
vm_syntax_error(lulu_VM &vm, String file, int line, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stdout,STRING_FMTSPEC ":%i: ", string_fmtarg(file), line);
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
    Chunk chunk = *vm.chunk;
    const Instruction *ip = raw_data(chunk.code);
    Slice<Value> window = vm.window;

#define GET_RK(rkb)                                                            \
    reg_is_rk(rkb)                                                             \
        ? chunk.constants[reg_get_k(rkb)]                                            \
        : window[getarg_b(rkb)]

#define ARITH_OP(fn)                                                           \
{                                                                              \
    u16 rkb = getarg_b(i);                                                     \
    u16 rkc = getarg_c(i);                                                     \
    Value rb = GET_RK(rkb);                                                    \
    Value rc = GET_RK(rkc);                                                    \
    ra = fn(rb, rc);                                                           \
}

    int pad = debug_get_pad(chunk);
    for (;;) {
        Instruction i  = *ip++;
        Value      &ra =  window[getarg_a(i)];

        for (size_t ii = 0, end = window.len; ii < end; ii++) {
            printf("\t[%zu]\t" LULU_NUMBER_FMT "\n", ii, window[ii]);
        }
        printf("\n");
        debug_disassemble_at(chunk, i, cast_int(ip - raw_data(chunk.code) - 1), pad);

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
        case OP_RETURN:
            return;
        }
    }
}
