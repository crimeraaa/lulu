#ifndef LULU_CHUNK_H
#define LULU_CHUNK_H

#include "value.h"

#define LULU_MAX_BYTE   (cast(byte)-1)
#define LULU_MAX_BYTE3  (cast(byte3)((1 << 24) - 1))

typedef enum {
    OP_CONSTANT,
    OP_GETGLOBAL, OP_SETGLOBAL,
    OP_GETLOCAL, OP_SETLOCAL,
    OP_NEWTABLE, OP_GETTABLE, OP_SETTABLE,
    OP_NIL, OP_TRUE, OP_FALSE,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW,
    OP_CONCAT,
    OP_UNM,
    OP_EQ, OP_LT, OP_LEQ, OP_NOT,
    OP_PRINT, // @note 2024-10-1: Temporary!
    OP_POP,
    OP_RETURN,
} OpCode;

#define LULU_OPCODE_COUNT (OP_RETURN + 1)

typedef struct {
    cstring name;
    i8      arg_size;   // Number of bytes used for the argument.
    i8      push_count; // Number values pushed from stack. -1 indicates unknown.
    i8      pop_count;  // Number values popped from stack. -1 indicates unknown.
} OpCode_Info;

extern const OpCode_Info
LULU_OPCODE_INFO[LULU_OPCODE_COUNT];

#define LULU_INSTRUCTION_OFFSET_A   24
#define LULU_INSTRUCTION_OFFSET_B   16
#define LULU_INSTRUCTION_OFFSET_C   8
#define LULU_INSTRUCTION_OFFSET_OP  0

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
instruction_make(OpCode op, byte a, byte b, byte c)
{
    return cast(Instruction)a << LULU_INSTRUCTION_OFFSET_A
        | cast(Instruction)b  << LULU_INSTRUCTION_OFFSET_B
        | cast(Instruction)c  << LULU_INSTRUCTION_OFFSET_C
        | cast(Instruction)op << LULU_INSTRUCTION_OFFSET_OP;
}

static inline Instruction
instruction_make_byte3(OpCode op, byte3 arg)
{
    // Note how we use msb w/ OFFSET_B, not OFFSET_A since we're masking bits.
    byte msb = (arg >> LULU_INSTRUCTION_OFFSET_B)  & LULU_MAX_BYTE;
    byte mid = (arg >> LULU_INSTRUCTION_OFFSET_C)  & LULU_MAX_BYTE;
    byte lsb = (arg >> LULU_INSTRUCTION_OFFSET_OP) & LULU_MAX_BYTE;
    return instruction_make(op, msb, mid, lsb);
}

#define instruction_make_byte1(op, arg)    instruction_make(op, arg, 0, 0)
#define instruction_make_none(op)          instruction_make(op, 0, 0, 0)
#define instruction_get_opcode(inst)       ((inst) & LULU_MAX_BYTE)

// When shifted, argument A has no extra upper bits.
#define instruction_get_A(inst)    ((inst) >> LULU_INSTRUCTION_OFFSET_A)
#define instruction_get_B(inst)    (((inst) >> LULU_INSTRUCTION_OFFSET_B) & LULU_MAX_BYTE)
#define instruction_get_C(inst)    (((inst) >> LULU_INSTRUCTION_OFFSET_C) & LULU_MAX_BYTE)
#define instruction_get_ABC(inst)  ((inst) >> LULU_INSTRUCTION_OFFSET_C)

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
    isize        len;      // Current number of actively used bytes in `code`.
    isize        cap;      // Total number of bytes that `code` points to.
} Chunk;

void
chunk_init(Chunk *self, cstring filename);

void
chunk_write(lulu_VM *vm, Chunk *self, Instruction inst, int line);

void
chunk_free(lulu_VM *vm, Chunk *self);

void
chunk_reserve(lulu_VM *vm, Chunk *self, isize new_cap);

/**
 * @brief
 *      Writes to the `self->constants` array and returns the index of the said
 *      value. This is so you can locate the constant later.
 */
isize
chunk_add_constant(lulu_VM *vm, Chunk *self, const Value *value);

#endif // LULU_CHUNK_H
