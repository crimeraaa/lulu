#include "debug.h"

#include <stdio.h>

void lulu_Debug_disasssemble_chunk(const lulu_Chunk *self, cstring name)
{
    printf("=== DISASSEMBLY: BEGIN ===\n");
    printf(".name \'%s\'\n", name);
    printf(".code\n");
    
    for (isize index = 0; index < self->len;) {
        index = lulu_Debug_disassemble_instruction(self, index);
    }
    
    printf("=== DISASSEMBLY: END ===\n\n");
}

static void print_arg_size_0(cstring name)
{
    printf("%s\n", name);
}

// static void print_arg_size_1(cstring name, const lulu_Chunk *chunk, isize index)
// {
//     byte arg = chunk->code[index + 1];
//     printf("%-16s %i \'", name, arg);
//     lulu_Debug_print_value(&chunk->constants.values[arg]);
//     printf("\'\n");
// }

static void print_constant(cstring name, const lulu_Chunk *chunk, isize index)
{
    Byte3 args;
    args[0] = chunk->code[index + 1];
    args[1] = chunk->code[index + 2];
    args[2] = chunk->code[index + 3];
    
    usize arg = (cast(usize)args[2] << 16) | (cast(usize)args[1] << 8) | cast(usize)args[0];
    printf("%-16s %4zu \'", name, arg);
    lulu_Debug_print_value(&chunk->constants.values[arg]);
    printf("\'\n");
}

isize lulu_Debug_disassemble_instruction(const lulu_Chunk *self, isize index)
{
    printf("%04ti ", index);
    
    if (index > 0 && self->lines[index] == self->lines[index - 1]) {
        printf("   | ");        
    } else {
        printf("%4i ", self->lines[index]);
    }
    
    byte inst = self->code[index];
    if (inst >= LULU_OPCODE_COUNT) {
        printf("Unknown opcode %i.\n", inst);
        return index + 1;
    }
    switch (inst) {
    case OP_CONSTANT:
        print_constant(LULU_OPCODE_INFO[inst].name, self, index);
        break;
    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_DIV:
    case OP_MOD:
    case OP_POW:
    case OP_NEGATE:
    case OP_RETURN:
        print_arg_size_0(LULU_OPCODE_INFO[inst].name);
        break;
    }
    return index + 1 + LULU_OPCODE_INFO[inst].arg_size;
}

void lulu_Debug_print_value(const lulu_Value *self)
{
    switch (self->type) {
    case LULU_VALUE_TYPE_NIL:
        printf("nil");
        break;
    case LULU_VALUE_TYPE_BOOLEAN:
        printf("%s", self->boolean ? "true" : "false");
        break;
    case LULU_VALUE_TYPE_NUMBER:
        printf("%g", self->number);
        break;
    }
}
