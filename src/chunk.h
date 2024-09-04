#ifndef LULU_CHUNK_H
#define LULU_CHUNK_H

#include "lulu.h"
#include "memory.h"
#include "value.h"

typedef enum {
    OP_CONSTANT,
    OP_RETURN,
} lulu_OpCode;

#define LULU_OPCODE_COUNT   (OP_RETURN + 1)

typedef struct {
    cstring name;
    i8      arg_size;
} lulu_OpCode_Info;

extern const lulu_OpCode_Info LULU_OPCODE_INFO[LULU_OPCODE_COUNT];

/**
 * @brief
 *      A 1D dynamic array of `byte`. This is the "byte" in "bytecode".
 */
typedef struct {
    lulu_Value_Array constants;
    byte *code;
    int  *lines; // Line numbers per bytecode.
    isize len;   // Current number of actively used bytes in `code`.
    isize cap;   // Total number of bytes that `code` points to.
} lulu_Chunk;

void lulu_Chunk_init(lulu_Chunk *self);
void lulu_Chunk_write(lulu_Chunk *self, byte inst, int line, const lulu_Allocator *a);
void lulu_Chunk_free(lulu_Chunk *self, const lulu_Allocator *a);

/**
 * @brief
 *      Writes to the `self->constants` array and returns the index of the said
 *      value. This is so you can locate the constant later.
 */
isize lulu_Chunk_add_constant(lulu_Chunk *self, const lulu_Value *v, const lulu_Allocator *a);

#endif // LULU_CHUNK_H
