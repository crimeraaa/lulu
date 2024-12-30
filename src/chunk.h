#ifndef LULU_CHUNK_H
#define LULU_CHUNK_H

#include "value.h"

#define LULU_MAX_BYTE   (cast(byte)-1)
#define LULU_MAX_BYTE3  (cast(byte3)((1 << 24) - 1))

typedef enum {
    OP_CONSTANT,
    OP_GET_GLOBAL, OP_SET_GLOBAL,
    OP_GET_LOCAL, OP_SET_LOCAL,
    OP_NEW_TABLE, OP_GET_TABLE, OP_SET_TABLE,
    OP_SET_ARRAY,
    OP_LEN,
    OP_NIL, OP_TRUE, OP_FALSE,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW,
    OP_CONCAT,
    OP_UNM,
    OP_EQ, OP_LT, OP_LEQ, OP_NOT,
    OP_PRINT, // @note 2024-10-1: Temporary!
    OP_POP,
    OP_RETURN,
} OpCode;

#define LULU_OPCODE_COUNT   (OP_RETURN + 1)

typedef struct {
    cstring name;
    i8      sz_arg; // Number of bytes used for the argument.
    i8      n_push; // Number values pushed from stack. -1 indicates unknown.
    i8      n_pop;  // Number values popped from stack. -1 indicates unknown.
} OpCode_Info;

extern const OpCode_Info
LULU_OPCODE_INFO[LULU_OPCODE_COUNT];

#define INSTR_OFFSET_A   24
#define INSTR_OFFSET_B   16
#define INSTR_OFFSET_C   8
#define INSTR_OFFSET_OP  0

#define INSTR_MASK_A     0xFF000000
#define INSTR_MASK_B     0x00FF0000
#define INSTR_MASK_C     0x0000FF00
#define INSTR_MASK_OP    0x000000FF
#define INSTR_MASK_ABC   0xFFFFFF00

/**
 * @brief Layout:
 *      [ arg A  ] [ arg B  ] [ arg C  ] [ opcode ]
 *      [ 31..24 ] [ 23..16 ] [ 15..08 ] [ 07..00 ]
 *
 * @details Example:
 *      arg A   =        3 =   00000011
 *      arg B   =        0 =            00000000
 *      arg C   =        0 =                     00000000
 *      opcode  =   OP_NIL =                              00000001
 *      inst    = 50331649 = 0b00000011_00000000_00000000_00000001
 */
typedef u32 Instruction;

static inline Instruction
instr_make(OpCode op, byte a, byte b, byte c)
{
    return cast(Instruction)a << INSTR_OFFSET_A
        | cast(Instruction)b  << INSTR_OFFSET_B
        | cast(Instruction)c  << INSTR_OFFSET_C
        | cast(Instruction)op << INSTR_OFFSET_OP;
}

static inline Instruction
instr_make_ABC(OpCode op, byte3 arg)
{
    // Note how we use msb w/ OFFSET_B, not OFFSET_A since we're masking bits.
    byte msb = (arg >> INSTR_OFFSET_B)  & LULU_MAX_BYTE;
    byte mid = (arg >> INSTR_OFFSET_C)  & LULU_MAX_BYTE;
    byte lsb = (arg >> INSTR_OFFSET_OP) & LULU_MAX_BYTE;
    return instr_make(op, msb, mid, lsb);
}

#define instr_make_A(op, arg)   instr_make(op, arg, 0, 0)

#define instr_get_op(inst)      (((inst) >> INSTR_OFFSET_OP) & LULU_MAX_BYTE)
#define instr_get_A(inst)       (((inst) >> INSTR_OFFSET_A)  & LULU_MAX_BYTE)
#define instr_get_B(inst)       (((inst) >> INSTR_OFFSET_B)  & LULU_MAX_BYTE)
#define instr_get_C(inst)       (((inst) >> INSTR_OFFSET_C)  & LULU_MAX_BYTE)
#define instr_get_ABC(inst)     ( (inst) >> INSTR_OFFSET_C)

static inline void
instr_set_A(Instruction *self, byte a)
{
    *self &= ~INSTR_MASK_A;
    *self |= cast(Instruction)a << INSTR_OFFSET_A;
}

static inline void
instr_set_B(Instruction *self, byte b)
{
    *self &= ~INSTR_MASK_B;
    *self |= cast(Instruction)b << INSTR_OFFSET_B;
}

static inline void
instr_set_ABC(Instruction *self, byte3 arg)
{
    *self &= ~INSTR_MASK_ABC;
    *self |= cast(Instruction)arg << INSTR_OFFSET_C;
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
    VArray       constants;
    Instruction *code;     // 1D array of bytecode.
    int         *lines;    // Line numbers per bytecode.
    cstring      filename; // Filename of where this chunk originated from.
    int          len;      // Current number of actively used bytes in `code`.
    int          cap;      // Total number of bytes that `code` points to.
} Chunk;

void
chunk_init(Chunk *self, cstring filename);

void
chunk_write(lulu_VM *vm, Chunk *self, Instruction inst, int line);

void
chunk_free(lulu_VM *vm, Chunk *self);

void
chunk_reserve(lulu_VM *vm, Chunk *self, int new_cap);

/**
 * @brief
 *      Writes to the `self->constants` array and returns the index of the said
 *      value. This is so you can locate the constant later.
 */
int
chunk_add_constant(lulu_VM *vm, Chunk *self, const Value *value);

#endif // LULU_CHUNK_H
