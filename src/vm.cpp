#include "vm.hpp"

void
vm_execute(lulu_VM &vm, Chunk &c)
{
    #define arith_op(op) \
    { \
        Value &rb = window[i.b()]; \
        Value &rc = window[i.c()]; \
        ra = op(rb, rc); \
    }

    const Instruction *ip = &c.code[0];
    Slice<Value> window{&vm.stack[0], static_cast<size_t>(c.stack_used)};
    
    for (auto &v : window) {
        v = 0.0;
    }

    for (;;) {
        Instruction i = *ip++;
        Value &ra = window[i.a()];
        switch (i.op()) {
        case OP_LOAD_CONSTANT:
            ra = c.constants[i.bx()];
            break;
        case OP_ADD: arith_op(lulu_Number_add); break;
        case OP_SUB: arith_op(lulu_Number_sub); break;
        case OP_MUL: arith_op(lulu_Number_mul); break;
        case OP_DIV: arith_op(lulu_Number_div); break;
        case OP_MOD: arith_op(lulu_Number_mod); break;
        case OP_POW: arith_op(lulu_Number_pow); break;
        case OP_RETURN: return;
        }
    }
}
