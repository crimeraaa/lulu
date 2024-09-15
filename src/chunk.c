#include "chunk.h"

const lulu_OpCode_Info
LULU_OPCODE_INFO[LULU_OPCODE_COUNT] = {
    //                name: cstring     arg_size,   push_count, pop_count: i8
    [OP_CONSTANT]   = {"CONSTANT",      3,           1,          0},
    [OP_NIL]        = {"NIL",           1,          -1,          0},
    [OP_TRUE]       = {"TRUE",          0,           1,          0},
    [OP_FALSE]      = {"FALSE",         0,           1,          0},
    [OP_ADD]        = {"ADD",           0,           0,          1},
    [OP_SUB]        = {"SUB",           0,           0,          1},
    [OP_MUL]        = {"MUL",           0,           0,          1},
    [OP_DIV]        = {"DIV",           0,           0,          1},
    [OP_MOD]        = {"MOD",           0,           0,          1},
    [OP_POW]        = {"POW",           0,           0,          1},
    [OP_UNM]        = {"UNM",           0,           0,          0},
    [OP_EQ]         = {"EQ",            0,           0,          1},
    [OP_LT]         = {"LT",            0,           0,          1},
    [OP_LEQ]        = {"LEQ",           0,           0,          1},
    [OP_NOT]        = {"NOT",           0,           0,          0},
    [OP_RETURN]     = {"RETURN",        0,           0,          1},
};

void
lulu_Chunk_init(lulu_Chunk *self, cstring filename)
{
    lulu_Value_Array_init(&self->constants);
    self->code     = NULL;
    self->lines    = NULL;
    self->filename = filename;
    self->len      = 0;
    self->cap      = 0;
}

void
lulu_Chunk_write(lulu_VM *vm, lulu_Chunk *self, lulu_Instruction inst, int line)
{
    // Appending to this index would cause a buffer overrun?
    isize index = self->len++;
    if (index >= self->cap) {
        lulu_Chunk_reserve(vm, self, GROW_CAPACITY(self->cap));
    }
    self->code[index]  = inst;
    self->lines[index] = line;
}

void
lulu_Chunk_reserve(lulu_VM *vm, lulu_Chunk *self, isize new_cap)
{
    isize old_cap = self->cap;
    // Nothing to do?
    if (new_cap <= old_cap) {
        return;
    }

    self->code  = rawarray_resize(lulu_Instruction, vm, self->code, old_cap, new_cap);
    self->lines = rawarray_resize(int, vm, self->lines, old_cap, new_cap);
    self->cap   = new_cap;
}

void
lulu_Chunk_free(lulu_VM *vm, lulu_Chunk *self)
{
    lulu_Value_Array_free(vm, &self->constants);
    rawarray_free(lulu_Instruction, vm, self->code, self->cap);
    rawarray_free(int, vm, self->lines, self->cap);
    lulu_Chunk_init(self, "(freed chunk)");
}

isize
lulu_Chunk_add_constant(lulu_VM *vm, lulu_Chunk *self, const lulu_Value *value)
{
    lulu_Value_Array *constants = &self->constants;
    for (isize i = 0; i < constants->len; i++) {
        if (lulu_Value_eq(value, &constants->values[i])) {
            return i;
        }
    }
    lulu_Value_Array_write(vm, constants, value);
    return constants->len - 1;
}
