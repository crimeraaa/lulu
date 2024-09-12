#include "debug.h"

#include <stdarg.h>
#include <stdio.h>

#undef lulu_Debug_writef

int
lulu_Debug_writef(cstring level, cstring file, int line, cstring fmt, ...)
{
    va_list argp;
    int writes = 0;
    va_start(argp, fmt);
    writes += fprintf(stderr, "[%s] %s:%i: ", level, file, line);
    writes += vfprintf(stderr, fmt, argp);
    fflush(stderr);
    va_end(argp);
    return writes;
}

void
lulu_Debug_print_value(const lulu_Value *value)
{
    switch (value->type) {
    case LULU_TYPE_NIL:
        printf("nil");
        break;
    case LULU_TYPE_BOOLEAN:
        printf("%s", value->boolean ? "true" : "false");
        break;
    case LULU_TYPE_NUMBER:
        printf(LULU_NUMBER_FMT, value->number);
        break;
    }
}

void
lulu_Debug_disasssemble_chunk(const lulu_Chunk *chunk)
{
    printf("=== DISASSEMBLY: BEGIN ===\n");
    printf(".name '%s'\n", chunk->filename);
    printf(".code\n");
    
    for (isize index = 0; index < chunk->len;) {
        index = lulu_Debug_disassemble_instruction(chunk, index);
    }
    
    printf("=== DISASSEMBLY: END ===\n\n");
}

static void
print_arg_size_0(cstring name)
{
    printf("%s\n", name);
}

/**
 * @note 2024-09-07
 *      Remember that for a 3-byte argument, the LSB is stored first.
 */
static void
print_constant(cstring name, const lulu_Chunk *chunk, lulu_Instruction inst)
{
    isize arg = lulu_Instruction_get_byte3(inst);
    printf("%-16s %4ti '", name, arg);
    lulu_Debug_print_value(&chunk->constants.values[arg]);
    printf("'\n");
}

isize
lulu_Debug_disassemble_instruction(const lulu_Chunk *chunk, isize index)
{
    printf("%04tx ", index);
    
    if (index > 0 && chunk->lines[index] == chunk->lines[index - 1]) {
        printf("   | ");        
    } else {
        printf("%4i ", chunk->lines[index]);
    }
    
    lulu_Instruction inst   = chunk->code[index];
    lulu_OpCode      opcode = lulu_Instruction_get_opcode(inst);
    if (opcode >= LULU_OPCODE_COUNT) {
        printf("Unknown opcode %i.\n", opcode);
        return index + 1;
    }
    switch (opcode) {
    case OP_CONSTANT:
        print_constant(LULU_OPCODE_INFO[opcode].name, chunk, inst);
        break;
    case OP_NIL: case OP_TRUE: case OP_FALSE:
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD: case OP_POW:
    case OP_UNM:
    case OP_EQ: case OP_LT: case OP_LEQ: case OP_NOT:
    case OP_RETURN:
        print_arg_size_0(LULU_OPCODE_INFO[opcode].name);
        break;
    }
    return index + 1;
}
