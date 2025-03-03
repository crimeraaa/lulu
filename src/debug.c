/// local
#include "debug.h"
#include "object.h"
#include "string.h"

/// standard
#include <stdarg.h> // va_list, va_start, va_end
#include <stdio.h>  // [vf]printf

#undef debug_writef

int
debug_writef(const char *level, const char *file, int line, const char *fmt, ...)
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
    const Value *constants = chunk->constants.values;
    for (int index = 0, stop = chunk->constants.len; index < stop; index++) {
        const Value *value = &constants[index];
        printf("%04i\t%s: ", index, value_typename(value));
        debug_print_value(value);
        printf("\n");
    }

    printf("\n.code\n");
    for (int index = 0, stop = chunk->len; index < stop; /* empty */) {
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
    const int    arg   = cast(int)instr_get_ABC(inst);
    const Value *value = &chunk->constants.values[arg];
    debug_print_value(value);
    printf("\n");
}

int
debug_disassemble_instruction(const Chunk *chunk, int index)
{
    printf("%04i ", index);
    if (index > 0 && chunk->lines[index] == chunk->lines[index - 1]) {
        printf("   | ");
    } else {
        printf("%4i ", chunk->lines[index]);
    }

    const Value *constants = chunk->constants.values;
    Instruction  inst      = chunk->code[index];
    OpCode       op        = instr_get_op(inst);

    printf("%-16s ", LULU_OPCODE_INFO[op].name);
    switch (op) {
    case OP_CONSTANT:
        print_constant(chunk, inst);
        break;
    case OP_GET_GLOBAL: case OP_SET_GLOBAL:
    {
        int arg = cast(int)instr_get_ABC(inst);
        printf("%4i\t# %s\n", arg, constants[arg].string->data);
        break;
    }
    case OP_NEW_TABLE:
    {
        int n_hash  = instr_get_A(inst);
        int n_array = instr_get_B(inst);
        printf("%4i, %i # hash=%i, array=%i\n", n_hash, n_array, n_hash, n_array);
        break;
    }
    case OP_SET_TABLE:
    {
        int n_pop   = instr_get_A(inst);
        int i_table = instr_get_B(inst);
        int i_key   = instr_get_C(inst);
        printf("%4i, %i, %i # table, key, pop\n", i_table, i_key, n_pop);
        break;
    }
    case OP_SET_ARRAY:
    {
        int n_pop   = instr_get_A(inst);
        int i_table = instr_get_B(inst);
        printf("%4i, %i # table, n_array\n", i_table, n_pop);
        break;
    }
    case OP_SET_LOCAL: case OP_GET_LOCAL:
    case OP_PRINT:
    case OP_POP:
    case OP_CONCAT:
    case OP_NIL:
    {
        int arg = instr_get_A(inst);
        printf("%4i\n", arg);
        break;
    }
    case OP_TRUE: case OP_FALSE:
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD: case OP_POW:
    case OP_UNM:
    case OP_EQ: case OP_LT: case OP_LEQ: case OP_NOT:
    case OP_RETURN:
    case OP_GET_TABLE:
    case OP_LEN:
        printf("\n");
        break;
    }
    return index + 1;
}
