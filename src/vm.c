#include "vm.h"

void
vm_init(lulu_VM *vm, lulu_Allocator allocator, void *allocator_data)
{
    vm->allocator      = allocator;
    vm->allocator_data = allocator_data;
}

void
vm_execute(lulu_VM *vm, Chunk *c)
{
    const Instruction *ip = dynamic_get_ptr(Instruction)(&c->code, 0);
    Dynamic(Value)     constants = c->constants;

#define ARITH_OP(fn) \
{ \
    Value rb = vm->stack[getarg_b(i)]; \
    Value rc = vm->stack[getarg_c(i)]; \
    *ra = fn(rb, rc); \
}

    for (;;) {
        Instruction i  = *ip++;
        Value      *ra = &vm->stack[getarg_a(i)];
        switch (getarg_op(i)) {
        case OP_LOAD_CONSTANT:
            *ra = dynamic_get(Value)(&constants, getarg_bx(i));
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
