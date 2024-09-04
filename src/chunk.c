#include "chunk.h"

const lulu_OpCode_Info LULU_OPCODE_INFO[LULU_OPCODE_COUNT] = {
    // lulu_OpCode     name: cstring    argsize: i8
    [OP_CONSTANT]   = {"CONSTANT",      1},
    [OP_RETURN]     = {"RETURN",        0},
};

void lulu_Chunk_init(lulu_Chunk *self)
{
    lulu_Value_Array_init(&self->constants);
    self->code  = NULL;
    self->lines = NULL;
    self->len   = 0;
    self->cap   = 0;
}

void lulu_Chunk_write(lulu_Chunk *self, byte inst, int line, const lulu_Allocator *a)
{
    // Appending to this index would cause a buffer overrun?
    if (self->len >= self->cap) {
        isize old_cap = self->cap;
        isize new_cap = GROW_CAPACITY(old_cap);

        /**
         * @warning 2024-09-04
         *      Ensure the given types AND pointers are correct!
         */
        self->code  = rawarray_resize(byte, self->code, old_cap, new_cap, a);
        self->lines = rawarray_resize(int, self->lines, old_cap, new_cap, a);
        self->cap   = new_cap;
    }
    isize index = self->len++;
    self->code[index]  = inst;
    self->lines[index] = line;
}

void lulu_Chunk_free(lulu_Chunk *self, const lulu_Allocator *a)
{
    lulu_Value_Array_free(&self->constants, a);
    /**
     * @warning 2024-09-04
     *      Ensure the provided type is correct!
     */
    rawarray_free(byte, self->code, self->cap, a);
    rawarray_free(int, self->lines, self->cap, a);
    lulu_Chunk_init(self);
}

isize lulu_Chunk_add_constant(lulu_Chunk *self, const lulu_Value *v, const lulu_Allocator *a)
{
    lulu_Value_Array_write(&self->constants, v, a);
    return self->constants.len - 1;
}
