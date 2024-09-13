#ifndef LULU_CHUNK_H
#define LULU_CHUNK_H

#include "lulu.h"
#include "memory.h"
#include "value.h"

typedef enum {
    OP_CONSTANT,
    OP_NIL, OP_TRUE, OP_FALSE,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW,
    OP_UNM,
    OP_EQ, OP_LT, OP_LEQ, OP_NOT,
    OP_RETURN,
} lulu_OpCode;

#define LULU_OPCODE_COUNT (OP_RETURN + 1)

typedef struct {
    cstring name;
    i8      arg_size;   // Number of bytes used for the argument.
    i8      push_count; // Number values pushed from stack. -1 indicates unknown.
    i8      pop_count;  // Number values popped from stack. -1 indicates unknown.
} lulu_OpCode_Info;

extern const lulu_OpCode_Info
LULU_OPCODE_INFO[LULU_OPCODE_COUNT];

/**
 * @brief Layout:
 *      [ arg A  ] [ arg B  ] [ arg C  ] [ opcode ]
 *      [ 31..24 ] [ 23..16 ] [ 15..08 ] [ 07..00 ]
 *
 * @details Example:
 *      opcode  =   OP_NIL =                              00000001
 *      byte1   =        3 =   00000011
 *      byte2   =        0 =            00000000
 *      byte3   =        0 =                     00000000
 *      inst    = 50331649 = 0b00000011_00000000_00000000_00000001
 */
typedef u32 lulu_Instruction;

static inline lulu_Instruction
lulu_Instruction_ABC(lulu_OpCode op, byte a, byte b, byte c)
{
    lulu_Instruction inst = 0;
    inst |= cast(lulu_Instruction)a << 24;
    inst |= cast(lulu_Instruction)b << 16;
    inst |= cast(lulu_Instruction)c << 8;
    inst |= cast(lulu_Instruction)op;
    return inst;
}

static inline lulu_Instruction
lulu_Instruction_byte3(lulu_OpCode op, byte3 arg)
{
    byte msb = (arg >> 16) & 0xff;
    byte mid = (arg >>  8) & 0xff;
    byte lsb = (arg >>  0) & 0xff;
    return lulu_Instruction_ABC(op, msb, mid, lsb);
}

static inline lulu_Instruction
lulu_Instruction_byte1(lulu_OpCode op, byte arg)
{
    return lulu_Instruction_ABC(op, arg, 0, 0);
}

static inline lulu_Instruction
lulu_Instruction_none(lulu_OpCode op)
{
    return lulu_Instruction_ABC(op, 0, 0, 0);
}

static inline lulu_OpCode
lulu_Instruction_get_opcode(lulu_Instruction inst)
{
    return cast(lulu_OpCode)(inst & 0xff);
}

static inline byte
lulu_Instruction_get_byte1(lulu_Instruction inst)
{
    return inst >> 24;
}

static inline byte3
lulu_Instruction_get_byte3(lulu_Instruction inst)
{
    return inst >> 8;
}

/**
 * @brief
 *      Contains the bytecode, line information and constant values.
 *
 * @note 2024-09-05
 *      Instead of explicitly storing the allocator we rely on the parent
 *      `lulu_VM *`.
 */
typedef struct {
    lulu_Value_Array  constants;
    lulu_Instruction *code;  // 1D array of bytecode.
    int    *lines; // Line numbers per bytecode.
    cstring filename;
    isize   len;   // Current number of actively used bytes in `code`.
    isize   cap;   // Total number of bytes that `code` points to.
} lulu_Chunk;

void
lulu_Chunk_init(lulu_Chunk *self, cstring filename);

void
lulu_Chunk_write(lulu_VM *vm, lulu_Chunk *self, lulu_Instruction inst, int line);

void
lulu_Chunk_free(lulu_VM *vm, lulu_Chunk *self);

void
lulu_Chunk_reserve(lulu_VM *vm, lulu_Chunk *self, isize new_cap);

/**
 * @brief
 *      Writes to the `self->constants` array and returns the index of the said
 *      value. This is so you can locate the constant later.
 */
isize
lulu_Chunk_add_constant(lulu_VM *vm, lulu_Chunk *self, const lulu_Value *value);

#endif // LULU_CHUNK_H
