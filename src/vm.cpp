#include <stdio.h>

#include "vm.h"

void
vm_init(lulu_VM &vm, lulu_Allocator allocator, void *allocator_data)
{
    vm.allocator      = allocator;
    vm.allocator_data = allocator_data;
}

void
vm_execute(lulu_VM &vm, Chunk &c)
{
    const Instruction *ip = &c.code[0];
    const auto &constants = c.constants;
    Slice<Value> window{vm.stack, cast(size_t, c.stack_used)};

#define GET_RK(rkb) \
    rk_is_rk(rkb) \
        ? constants[rk_get_k(rkb)] \
        : window[getarg_b(rkb)]

#define ARITH_OP(fn) \
{ \
    u16 b = getarg_b(i); \
    u16 c = getarg_c(i); \
    Value rb = GET_RK(b); \
    Value rc = GET_RK(c); \
    ra = fn(rb, rc); \
}

    for (;;) {
        Instruction i  = *ip++;
        Value      &ra =  window[getarg_a(i)];

        for (size_t ii = 0, end = window.len; ii < end; ii++) {
            printf("\t[%zu]\t" LULU_NUMBER_FMT "\n", ii, window[ii]);
        }
        printf("\n");
        chunk_dump_instruction(c, i, cast_int(ip - c.code.data) - 1);

        switch (getarg_op(i)) {
        case OP_LOAD_CONSTANT:
            ra = constants[getarg_bx(i)];
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
