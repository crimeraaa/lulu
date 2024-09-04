#ifndef LULU_CHUNK_H
#define LULU_CHUNK_H

#include "lulu.h"
#include "memory.h"
#include "value.h"

typedef enum {
    OP_CONSTANT,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_POW,
    OP_NEGATE,
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
 *      Contains the bytecode, line information and constant values. Remembers
 *      its allocator simliar to Odin `dynamic[$T]`.
 */
typedef struct {
    lulu_Value_Array constants; // Also contains the allocator.
    byte *code;  // 1D array of bytecode.
    int * lines; // Line numbers per bytecode.
    isize len;   // Current number of actively used bytes in `code`.
    isize cap;   // Total number of bytes that `code` points to.
} lulu_Chunk;

typedef byte Byte3[3];
typedef struct {
    byte *bytes;
    isize len;
} Byte_Slice;

void lulu_Chunk_init(lulu_Chunk *self, const lulu_Allocator *allocator);
void lulu_Chunk_write(lulu_Chunk *self, byte inst, int line);
void lulu_Chunk_free(lulu_Chunk *self);
void lulu_Chunk_reserve(lulu_Chunk *self, isize new_cap);

/**
 * @brief
 *      Using bit manipulation, encode `inst` into 3 separate bytes.
 * 
 * @note 2024-09-04
 *      We encode in little endian fashion: LSB first and MSB last.
 */
void lulu_Chunk_write_byte3(lulu_Chunk *self, usize inst, int line);
void lulu_Chunk_write_bytes(lulu_Chunk *self, Byte_Slice bytes, int line);

/**
 * @brief
 *      Writes to the `self->constants` array and returns the index of the said
 *      value. This is so you can locate the constant later.
 */
isize lulu_Chunk_add_constant(lulu_Chunk *self, const lulu_Value *value);

#endif // LULU_CHUNK_H
