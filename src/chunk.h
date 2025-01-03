#ifndef LULU_CHUNK_H
#define LULU_CHUNK_H

#include "value.h"

#define LULU_MAX_BYTE   (cast(byte)-1)
#define LULU_MAX_BYTE3  (cast(u32)((1 << 24) - 1))

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

#define INSTR_SIZE_A        9
#define INSTR_SIZE_B        9
#define INSTR_SIZE_C        8
#define INSTR_SIZE_OP       6
#define INSTR_SIZE_ABC      (INSTR_SIZE_A + INSTR_SIZE_B + INSTR_SIZE_C)

#define INSTR_MAX_A         ((1 << INSTR_SIZE_A)  - 1)
#define INSTR_MAX_B         ((1 << INSTR_SIZE_B)  - 1)
#define INSTR_MAX_C         ((1 << INSTR_SIZE_C)  - 1)
#define INSTR_MAX_OP        ((1 << INSTR_SIZE_OP) - 1)
#define INSTR_MAX_ABC       ((1 << INSTR_SIZE_ABC) - 1)

#define INSTR_OFFSET_OP     0
#define INSTR_OFFSET_C      (INSTR_OFFSET_OP + INSTR_SIZE_OP) // 0 + 6  = 6
#define INSTR_OFFSET_B      (INSTR_OFFSET_C  + INSTR_SIZE_C)  // 6 + 8  = 14
#define INSTR_OFFSET_A      (INSTR_OFFSET_B  + INSTR_SIZE_B)  // 14 + 9 = 23
#define INSTR_OFFSET_ABC    INSTR_OFFSET_C


/**
 * @brief
 *      Creates a bitmask of type 'Instruction' where `n_onebits` 1-bits are
 *      found starting at the `n_position`th bit.
 * @note
 *      This one is hard for me to wrap my head around so just be happy it works!
 *      https://www.lua.org/source/5.1/lopcodes.h.html#MASK1
 */
#define INSTR_MAKE_MASK1(n_onebits, n_position)                                \
    (                                                                          \
        (                                                                      \
            ~( (~(Instruction)0) << (n_onebits) )                              \
        ) << (n_position)                                                      \
    )

#define INSTR_MAKE_MASK0(n_zerobits, n_position) \
    ~INSTR_MAKE_MASK1(n_zerobits, n_position)

// MASK1 is used to extract the desired operand from a right-shifted 'Instruction'.
#define INSTR_MASK1_A       INSTR_MAKE_MASK1(INSTR_SIZE_A,      0)
#define INSTR_MASK1_B       INSTR_MAKE_MASK1(INSTR_SIZE_B,      0)
#define INSTR_MASK1_C       INSTR_MAKE_MASK1(INSTR_SIZE_C,      0)
#define INSTR_MASK1_OP      INSTR_MAKE_MASK1(INSTR_SIZE_OP,     0)
#define INSTR_MASK1_ABC     INSTR_MAKE_MASK1(INSTR_SIZE_ABC,    0)

// MASK0 is used for clearing the 'Instruction' desired operand's bits.
#define INSTR_MASK0_A       INSTR_MAKE_MASK0(INSTR_SIZE_A,      INSTR_OFFSET_A)
#define INSTR_MASK0_B       INSTR_MAKE_MASK0(INSTR_SIZE_B,      INSTR_OFFSET_B)
#define INSTR_MASK0_C       INSTR_MAKE_MASK0(INSTR_SIZE_C,      INSTR_OFFSET_C)
#define INSTR_MASK0_OP      INSTR_MAKE_MASK0(INSTR_SIZE_OP,     INSTR_OFFSET_OP)
#define INSTR_MASK0_ABC     INSTR_MAKE_MASK0(INSTR_SIZE_ABC,    INSTR_OFFSET_ABC)

/**
 * @brief Layout:
 *      [ arg A  ] [ arg B  ] [ arg C  ] [ opcode ]
 *      [ 31..23 ] [ 22..14 ] [ 13..06 ] [ 05..00 ]
 *
 * @details Example:
 *      arg A   =        3 = 0_0000_0011
 *      arg B   =        0 =              0_0000_0000
 *      arg C   =        0 =                           0000_0000
 *      opcode  =   OP_NIL =                                      00_0001
 *      inst    = 50331649 = 0_0000_0011  0_0000_0000  0000_0000  00_0001
 */
typedef u32 Instruction;

static inline Instruction
instr_make(OpCode op, u16 a, u16 b, u16 c)
{
    return cast(Instruction)a << INSTR_OFFSET_A
        | cast(Instruction)b  << INSTR_OFFSET_B
        | cast(Instruction)c  << INSTR_OFFSET_C
        | cast(Instruction)op << INSTR_OFFSET_OP;
}

static inline Instruction
instr_make_ABC(OpCode op, u32 abc)
{
    // Note how we use OFFSET_B, not OFFSET_A since the 0th bit of 'abc'
    // is the start of 'c'.
    u16 a = (abc >> INSTR_OFFSET_B)  & INSTR_MAX_A;
    u16 b = (abc >> INSTR_OFFSET_C)  & INSTR_MAX_B;
    u16 c = (abc >> INSTR_OFFSET_OP) & INSTR_MAX_C;
    return instr_make(op, a, b, c);
}

#define instr_make_A(op, abc)   instr_make(op, abc, 0, 0)

static inline OpCode
instr_get_op(Instruction inst)
{
    return (inst >> INSTR_OFFSET_OP) & INSTR_MASK1_OP;
}

static inline u16
instr_get_A(Instruction inst)
{
    return (inst >> INSTR_OFFSET_A) & INSTR_MASK1_A;
}

static inline u16
instr_get_B(Instruction inst)
{
    return (inst >> INSTR_OFFSET_B) & INSTR_MASK1_B;
}

static inline u16
instr_get_C(Instruction inst)
{
    return (inst >> INSTR_OFFSET_C) & INSTR_MASK1_C;
}

static inline u32
instr_get_ABC(Instruction inst)
{
    return inst >> INSTR_OFFSET_C;
}

static inline void
instr_set_A(Instruction *self, u16 a)
{
    // @warning explicit cast: u16=>Instruction to ensure left-shift does not truncate.
    *self &= INSTR_MASK0_A;
    *self |= cast(Instruction)a << INSTR_OFFSET_A;
}

static inline void
instr_set_B(Instruction *self, u16 b)
{
    *self &= INSTR_MASK0_B;
    *self |= cast(Instruction)b << INSTR_OFFSET_B;
}

static inline void
instr_set_C(Instruction *self, u16 c)
{
    *self &= INSTR_MASK0_C;
    *self |= cast(Instruction)c << INSTR_OFFSET_C;
}

static inline void
instr_set_ABC(Instruction *self, u32 abc)
{
    *self &= INSTR_MASK0_ABC;
    *self |= cast(Instruction)abc << INSTR_OFFSET_ABC;
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
