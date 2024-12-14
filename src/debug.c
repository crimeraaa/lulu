#include "debug.h"
#include "object.h"
#include "string.h"

#include <stdarg.h>
#include <stdio.h>

#undef lulu_Debug_writef

int
lulu_Debug_writef(cstring level, cstring file, int line, cstring fmt, ...)
{
    va_list argp;
    int     writes = 0;
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
    bool is_string = lulu_Value_is_string(value);
    char quote     = (is_string && value->string->len == 1) ? '\'' : '\"';
    if (is_string) {
        printf("%c", quote);
    }
    lulu_Value_print(value);
    if (is_string) {
        printf("%c", quote);
    }
}

void
lulu_Debug_disasssemble_chunk(const lulu_Chunk *chunk)
{
    printf("=== DISASSEMBLY: BEGIN ===\n");
    printf(".name \"%s\"\n", chunk->filename);

    printf("\n.const\n");
    const lulu_Value_Array *constants = &chunk->constants;
    for (isize index = 0; index < constants->len; index++) {
        const lulu_Value *value = &constants->values[index];
        printf("%04tx\t%s: ", index, lulu_Value_typename(value));
        lulu_Debug_print_value(value);
        printf("\n");
    }

    printf("\n.code\n");
    for (isize index = 0; index < chunk->len;) {
        index = lulu_Debug_disassemble_instruction(chunk, index);
    }
    printf("=== DISASSEMBLY: END ===\n\n");
}

/**
 * @note 2024-09-07
 *      Remember that for a 3-byte argument, the LSB is stored first.
 */
static void
print_constant(const lulu_Chunk *chunk, lulu_Instruction inst)
{
    isize             arg   = lulu_Instruction_get_byte3(inst);
    const lulu_Value *value = &chunk->constants.values[arg];
    printf("%4ti\t# %s: ", arg, lulu_Value_typename(value));
    lulu_Debug_print_value(value);
    printf("\n");
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

    const lulu_Value *constants = chunk->constants.values;
    lulu_Instruction  inst = chunk->code[index];
    lulu_OpCode       op   = lulu_Instruction_get_opcode(inst);

    printf("%-16s ", LULU_OPCODE_INFO[op].name);
    switch (op) {
    case OP_CONSTANT:
        print_constant(chunk, inst);
        break;
    case OP_GETGLOBAL: case OP_SETGLOBAL:
    {
        byte3 arg = lulu_Instruction_get_byte3(inst);
        printf("%4i\t# %s\n", arg, constants[arg].string->data);
        break;
    }
    case OP_NEWTABLE:
    {
        byte3 arg = lulu_Instruction_get_byte3(inst);
        printf("%4i\n", arg);
        break;
    }
    case OP_SETLOCAL: case OP_GETLOCAL:
    case OP_PRINT:
    case OP_POP:
    case OP_CONCAT:
    case OP_NIL:
    {
        byte arg = lulu_Instruction_get_byte1(inst);
        printf("%4i\n", arg);
        break;
    }
    case OP_TRUE: case OP_FALSE:
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD: case OP_POW:
    case OP_UNM:
    case OP_EQ: case OP_LT: case OP_LEQ: case OP_NOT:
    case OP_RETURN:
    case OP_GETTABLE: case OP_SETTABLE:
        printf("\n");
        break;
    }
    return index + 1;
}
