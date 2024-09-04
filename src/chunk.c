#include "chunk.h"

const lulu_OpCode_Info LULU_OPCODE_INFO[LULU_OPCODE_COUNT] = {
    // lulu_OpCode     name: cstring    argsize: i8
    [OP_CONSTANT]   = {"CONSTANT",      3},
    [OP_RETURN]     = {"RETURN",        0},
};

void lulu_Chunk_init(lulu_Chunk *self, const lulu_Allocator *allocator)
{
    lulu_Value_Array_init(&self->constants, allocator);
    self->code      = NULL;
    self->lines     = NULL;
    self->len       = 0;
    self->cap       = 0;
}

void lulu_Chunk_write(lulu_Chunk *self, byte inst, int line)
{
    // Appending to this index would cause a buffer overrun?
    if (self->len >= self->cap) {
        lulu_Chunk_reserve(self, GROW_CAPACITY(self->cap));
    }
    isize index = self->len++;
    self->code[index]  = inst;
    self->lines[index] = line;
}

void lulu_Chunk_reserve(lulu_Chunk *self, isize new_cap)
{
    const lulu_Allocator *allocator = &self->constants.allocator;
    isize old_cap = self->cap;
    // Nothing to do?
    if (new_cap <= old_cap) {
        return;
    }

    /**
     * @warning 2024-09-04
     *      Ensure the given types AND pointers are correct!
     */
    self->code  = rawarray_resize(byte, self->code, old_cap, new_cap, allocator);
    self->lines = rawarray_resize(int, self->lines, old_cap, new_cap, allocator);
    self->cap   = new_cap;
}

void lulu_Chunk_write_byte3(lulu_Chunk *self, usize inst, int line)
{
    Byte3 _buf;
    Byte_Slice args = {_buf, size_of(_buf)};

    args.bytes[0] = inst & 0xff;         // bits 0..7
    args.bytes[1] = (inst >> 8)  & 0xff; // bits 8..15
    args.bytes[2] = (inst >> 16) & 0xff; // bits 16..23

    lulu_Chunk_write_bytes(self, args, line);
}

void lulu_Chunk_write_bytes(lulu_Chunk *self, Byte_Slice bytes, int line)
{
    isize old_len = self->len;
    isize new_len = old_len + bytes.len;
    if (new_len > self->cap) {
        // Grow to the next power of 2
        isize new_cap = 1;
        while (new_cap < new_len) {
            new_cap *= 2;
        }
        lulu_Chunk_reserve(self, new_cap);
    }
    for (isize i = 0; i < bytes.len; i++) {
        self->code[old_len + i]  = bytes.bytes[i];
        self->lines[old_len + i] = line;
    }
    self->len = new_len;
}

void lulu_Chunk_free(lulu_Chunk *self)
{
    const lulu_Allocator *allocator = &self->constants.allocator;
    lulu_Value_Array_free(&self->constants);
    /**
     * @warning 2024-09-04
     *      Ensure the provided type is correct!
     */
    rawarray_free(byte, self->code, self->cap, allocator);
    rawarray_free(int, self->lines, self->cap, allocator);
    lulu_Chunk_init(self, allocator);
}

isize lulu_Chunk_add_constant(lulu_Chunk *self, const lulu_Value *value)
{
    lulu_Value_Array_write(&self->constants, value);
    return self->constants.len - 1;
}
