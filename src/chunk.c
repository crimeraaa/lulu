#include "chunk.h"

const lulu_OpCode_Info LULU_OPCODE_INFO[LULU_OPCODE_COUNT] = {
    //                name: cstring     arg_size, push_count, pop_count: i8
    [OP_CONSTANT]   = {"CONSTANT",      3,        1,          0},
    [OP_ADD]        = {"ADD",           0,        1,          2},
    [OP_SUB]        = {"SUB",           0,        1,          2},
    [OP_MUL]        = {"MUL",           0,        1,          2},
    [OP_DIV]        = {"DIV",           0,        1,          2},
    [OP_MOD]        = {"MOD",           0,        1,          2},
    [OP_POW]        = {"POW",           0,        1,          2},
    [OP_UNM]        = {"UNM",           0,        1,          1},
    [OP_RETURN]     = {"RETURN",        0,        0,          1},
};

void lulu_Chunk_init(lulu_Chunk *self)
{
    lulu_Value_Array_init(&self->constants);
    self->code      = NULL;
    self->lines     = NULL;
    self->len       = 0;
    self->cap       = 0;
}

void lulu_Chunk_write(lulu_VM *vm, lulu_Chunk *self, byte inst, int line)
{
    // Appending to this index would cause a buffer overrun?
    if (self->len >= self->cap) {
        lulu_Chunk_reserve(vm, self, GROW_CAPACITY(self->cap));
    }
    isize index = self->len++;
    self->code[index]  = inst;
    self->lines[index] = line;
}

void lulu_Chunk_reserve(lulu_VM *vm, lulu_Chunk *self, isize new_cap)
{
    isize old_cap = self->cap;
    // Nothing to do?
    if (new_cap <= old_cap) {
        return;
    }

    self->code  = rawarray_resize(byte, vm, self->code, old_cap, new_cap);
    self->lines = rawarray_resize(int, vm, self->lines, old_cap, new_cap);
    self->cap   = new_cap;
}

void lulu_Chunk_write_byte3(lulu_VM *vm, lulu_Chunk *self, byte3 inst, int line)
{
    byte lsb = inst & 0xff;         // bits 0..7
    byte mid = (inst >> 8)  & 0xff; // bits 8..15
    byte msb = (inst >> 16) & 0xff; // bits 16..23
    byte buf[] = {lsb, mid, msb};
    lulu_Chunk_write_bytes(vm, self, buf, size_of(buf), line);
}

void lulu_Chunk_write_bytes(lulu_VM *vm, lulu_Chunk *self, const byte *bytes, isize count, int line)
{
    isize old_len = self->len;
    isize new_len = old_len + count;
    if (new_len > self->cap) {
        // Grow to the next power of 2
        isize new_cap = 1;
        while (new_cap < new_len) {
            new_cap *= 2;
        }
        lulu_Chunk_reserve(vm, self, new_cap);
    }
    for (isize i = 0; i < count; i++) {
        self->code[old_len + i]  = bytes[i];
        self->lines[old_len + i] = line;
    }
    self->len = new_len;
}

void lulu_Chunk_free(lulu_VM *vm, lulu_Chunk *self)
{
    lulu_Value_Array_free(vm, &self->constants);
    rawarray_free(byte, vm, self->code, self->cap);
    rawarray_free(int, vm, self->lines, self->cap);
    lulu_Chunk_init(self);
}

isize lulu_Chunk_add_constant(lulu_VM *vm, lulu_Chunk *self, const lulu_Value *value)
{
    lulu_Value_Array_write(vm, &self->constants, value);
    return self->constants.len - 1;
}
