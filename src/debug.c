#include "debug.h"

#include <stdio.h>

void lulu_Debug_print_value(const lulu_Value *value)
{
    switch (value->type) {
    case LULU_VALUE_NIL:
        printf("nil");
        break;
    case LULU_VALUE_BOOLEAN:
        printf("%s", value->boolean ? "true" : "false");
        break;
    case LULU_VALUE_NUMBER:
        printf("%g", value->number);
        break;
    }
}

void lulu_Debug_disasssemble_chunk(const lulu_Chunk *chunk, cstring name)
{
    printf("=== DISASSEMBLY: BEGIN ===\n");
    printf(".name \'%s\'\n", name);
    printf(".code\n");
    
    for (isize index = 0; index < chunk->len;) {
        index = lulu_Debug_disassemble_instruction(chunk, index);
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
    byte lsb = chunk->code[index + 1];
    byte mid = chunk->code[index + 2];
    byte msb = chunk->code[index + 3];
    
    usize arg = lsb | (mid << 8) | (msb << 16);
    printf("%-16s %4zu \'", name, arg);
    lulu_Debug_print_value(&chunk->constants.values[arg]);
    printf("\'\n");
}

isize lulu_Debug_disassemble_instruction(const lulu_Chunk *chunk, isize index)
{
    printf("%04ti ", index);
    
    if (index > 0 && chunk->lines[index] == chunk->lines[index - 1]) {
        printf("   | ");        
    } else {
        printf("%4i ", chunk->lines[index]);
    }
    
    byte inst = chunk->code[index];
    if (inst >= LULU_OPCODE_COUNT) {
        printf("Unknown opcode %i.\n", inst);
        return index + 1;
    }
    switch (inst) {
    case OP_CONSTANT:
        print_constant(LULU_OPCODE_INFO[inst].name, chunk, index);
        break;
    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_DIV:
    case OP_MOD:
    case OP_POW:
    case OP_UNM:
    case OP_RETURN:
        print_arg_size_0(LULU_OPCODE_INFO[inst].name);
        break;
    }
    return index + 1 + LULU_OPCODE_INFO[inst].arg_size;
}
