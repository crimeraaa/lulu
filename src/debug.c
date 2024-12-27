#include "debug.h"
#include "object.h"
#include "string.h"

#include <stdarg.h>
#include <stdio.h>

#undef debug_writef

int
debug_writef(cstring level, cstring file, int line, cstring fmt, ...)
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
debug_print_value(const Value *value)
{
    bool is_string = value_is_string(value);
    char quote     = (is_string && value->string->len == 1) ? '\'' : '\"';
    if (is_string) {
        printf("%c", quote);
    }
    value_print(value);
    if (is_string) {
        printf("%c", quote);
    }
}

void
debug_disasssemble_chunk(const Chunk *chunk)
{
    printf("=== DISASSEMBLY: BEGIN ===\n");
    printf(".name \"%s\"\n", chunk->filename);

    printf("\n.const\n");
    const VArray *constants = &chunk->constants;
    for (isize index = 0; index < constants->len; index++) {
        const Value *value = &constants->values[index];
        printf("%04tx\t%s: ", index, value_typename(value));
        debug_print_value(value);
        printf("\n");
    }

    printf("\n.code\n");
    for (isize index = 0; index < chunk->len;) {
        index = debug_disassemble_instruction(chunk, index);
    }
    printf("=== DISASSEMBLY: END ===\n\n");
}

/**
 * @note 2024-09-07
 *      Remember that for a 3-byte argument, the LSB is stored first.
 */
static void
print_constant(const Chunk *chunk, Instruction inst)
{
    const isize       arg   = instruction_get_ABC(inst);
    const Value *value = &chunk->constants.values[arg];
    printf("%4ti\t# %s: ", arg, value_typename(value));
    debug_print_value(value);
    printf("\n");
}

isize
debug_disassemble_instruction(const Chunk *chunk, isize index)
{
    printf("%04tx ", index);
    if (index > 0 && chunk->lines[index] == chunk->lines[index - 1]) {
        printf("   | ");
    } else {
        printf("%4i ", chunk->lines[index]);
    }

    const Value *constants = chunk->constants.values;
    Instruction  inst = chunk->code[index];
    OpCode       op   = instruction_get_opcode(inst);

    printf("%-16s ", LULU_OPCODE_INFO[op].name);
    switch (op) {
    case OP_CONSTANT:
        print_constant(chunk, inst);
        break;
    case OP_GETGLOBAL: case OP_SETGLOBAL:
    {
        byte3 arg = instruction_get_ABC(inst);
        printf("%4i\t# %s\n", arg, constants[arg].string->data);
        break;
    }
    case OP_NEWTABLE:
    {
        byte3 arg = instruction_get_ABC(inst);
        printf("%4i\n", arg);
        break;
    }
    case OP_SETTABLE:
    {
        int i_table = instruction_get_A(inst);
        int i_key   = instruction_get_B(inst);
        int n_pop   = instruction_get_C(inst);
        printf("%4i (table), %i (key), pop %i\n", i_table, i_key, n_pop);
        break;
    }
    case OP_SETLOCAL: case OP_GETLOCAL:
    case OP_PRINT:
    case OP_POP:
    case OP_CONCAT:
    case OP_NIL:
    {
        byte arg = instruction_get_A(inst);
        printf("%4i\n", arg);
        break;
    }
    case OP_TRUE: case OP_FALSE:
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD: case OP_POW:
    case OP_UNM:
    case OP_EQ: case OP_LT: case OP_LEQ: case OP_NOT:
    case OP_RETURN:
    case OP_GETTABLE:
        printf("\n");
        break;
    }
    return index + 1;
}
