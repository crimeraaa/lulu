#include "vm.h"
#include "debug.h"

#include <stdio.h>

static void lulu_VM_reset_stack(lulu_VM *self)
{
    self->stack_top = self->stack;
}

void lulu_VM_init(lulu_VM *self, lulu_Allocator allocator, void *allocator_data)
{
    self->allocator      = allocator;
    self->allocator_data = allocator_data;
    self->chunk          = NULL;
    lulu_VM_reset_stack(self);
}

void lulu_VM_free(lulu_VM *self)
{
    unused(self);
}

static lulu_Status lulu_VM_execute(lulu_VM *self)
{

#define READ_BYTE()     (*self->ip++)
#define READ_BYTE3()    READ_BYTE() | (READ_BYTE() << 8) | (READ_BYTE() << 16)
#define READ_CONSTANT() (self->chunk->constants.values[READ_BYTE3()])

#define BINARY_OP(lulu_Number_fn)                                              \
do {                                                                           \
    lulu_Value rhs = lulu_VM_pop(self);                                        \
    lulu_Value lhs = lulu_VM_pop(self);                                        \
    lulu_Value result;                                                         \
    lulu_Value_set_number(&result, lulu_Number_fn(lhs.number, rhs.number));    \
    lulu_VM_push(self, &result);                                               \
} while (0)
    
    for (;;) {
#ifdef LULU_DEBUG_TRACE
        printf("        ");
        for (const lulu_Value *slot = self->stack; slot < self->stack_top; slot++) {
            printf("[ ");
            lulu_Debug_print_value(slot);
            printf(" ]");
        }
        printf("\n");
        lulu_Debug_disassemble_instruction(self->chunk, self->ip - self->chunk->code);
#endif
        byte inst = READ_BYTE();
        switch (inst) {
        case OP_CONSTANT: {
            lulu_Value value = READ_CONSTANT();
            lulu_VM_push(self, &value);
            break;
        }
        case OP_ADD: BINARY_OP(lulu_Number_add); break;
        case OP_SUB: BINARY_OP(lulu_Number_sub); break;
        case OP_MUL: BINARY_OP(lulu_Number_mul); break;
        case OP_DIV: BINARY_OP(lulu_Number_div); break;
        case OP_MOD: BINARY_OP(lulu_Number_mod); break;
        case OP_POW: BINARY_OP(lulu_Number_pow); break;
        
        case OP_NEGATE: {
            lulu_Value value = lulu_VM_pop(self);
            lulu_Value_set_number(&value, -value.number);
            lulu_VM_push(self, &value);
            break;
        }
        case OP_RETURN: {
            lulu_Value value = lulu_VM_pop(self);
            lulu_Debug_print_value(&value);
            printf("\n");
            return LULU_OK;
        }
        }
    }
    
#undef BINARY_OP
#undef READ_BYTE
#undef READ_BYTE3
#undef READ_CONSTANT

}

lulu_Status lulu_VM_interpret(lulu_VM *self, lulu_Chunk *chunk)
{
    self->chunk = chunk;
    self->ip    = chunk->code;
    return lulu_VM_execute(self);
}

void lulu_VM_push(lulu_VM *self, const lulu_Value *value)
{
    *self->stack_top = *value;
    self->stack_top++;
}

lulu_Value lulu_VM_pop(lulu_VM *self)
{
    self->stack_top--;
    return *self->stack_top;
}
