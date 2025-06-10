#pragma once

#include "private.h"

typedef enum {
    OP_LOAD_CONSTANT,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_POW,
    OP_RETURN,
} OpCode;

#define OPCODE_COUNT        OP_RETURN + 1

#define OPCODE_SIZE_B       9
#define OPCODE_SIZE_C       9
#define OPCODE_SIZE_A       8
#define OPCODE_SIZE_OP      6
#define OPCODE_SIZE_BX      (OPCODE_SIZE_B + OPCODE_SIZE_C)

#define OPCODE_BIT_RK       (1 << (OPCODE_SIZE_B - 1))
#define OPCODE_MAX_RK     (OPCODE_BIT_RK - 1)

#define OPCODE_OFFSET_B     (OPCODE_OFFSET_C + OPCODE_SIZE_C)
#define OPCODE_OFFSET_C     (OPCODE_OFFSET_A + OPCODE_SIZE_A)
#define OPCODE_OFFSET_A     (OPCODE_OFFSET_OP + OPCODE_SIZE_OP)
#define OPCODE_OFFSET_OP    0
#define OPCODE_OFFSET_BX    OPCODE_OFFSET_C

// Fills the `n` lower bits with 1's.
// Useful when reading bit fields.
#define BITMASK1(n)          ((1 << (n)) - 1)

// Set `start` up to `start + n` bits with 0's. All the rest are 1's.
// Useful when setting bit fields.
#define BITMASK0(start, n)  (~cast(uint32_t, BITMASK1(start) << (n)))

#define OPCODE_MAX_B        BITMASK1(OPCODE_SIZE_B)
#define OPCODE_MAX_C        BITMASK1(OPCODE_SIZE_C)
#define OPCODE_MAX_A        BITMASK1(OPCODE_SIZE_A)
#define OPCODE_MAX_OP       BITMASK1(OPCODE_SIZE_OP)
#define OPCODE_MAX_BX       BITMASK1(OPCODE_SIZE_BX)
#define OPCODE_MAX_SBX      (OPCODE_MAX_BX >> 1)

#define OPCODE_MASK0_B      BITMASK0(OPCODE_SIZE_B, OPCODE_OFFSET_B)
#define OPCODE_MASK0_C      BITMASK0(OPCODE_SIZE_C, OPCODE_OFFSET_C)
#define OPCODE_MASK0_A      BITMASK0(OPCODE_SIZE_A, OPCODE_OFFSET_A)
#define OPCODE_MASK0_OP     BITMASK0(OPCODE_SIZE_OP, OPCODE_OFFSET_OP)
#define OPCODE_MASK0_BX     BITMASK0(OPCODE_SIZE_BX, OPCODE_OFFSET_BX)

/**
 * @details 2025-06-10
 *  +--------+--------+--------+--------+
 *  | 31..23 | 22..14 | 13..06 | 05:00  |
 *  +--------+--------+--------+--------+
 *  | Arg(B) | Arg(C) | Arg(A) | OpCode |
 *  +--------+--------+--------+--------+
 */
typedef uint32_t Instruction;

typedef enum {
    OPFORMAT_ABC,
    OPFORMAT_ABX,
    OPFORMAT_ASBX,
} OpFormat;

// A bit space inefficient but I find this easier to reason about.
typedef enum {
    OPARG_UNUSED,       // 0b0000
    OPARG_REG           = 1 << 0, // 0b0001: Is a register only
    OPARG_CONSTANT      = 1 << 1, // 0b0010: Is a constant index only
    OPARG_REG_CONSTANT,           // 0b0011: Is a register OR a constant
    OPARG_JUMP          = 1 << 2, // 0b0100
    OPARG_TEST          = 1 << 3, // 0b1000
} OpArg;

#define OPINFO_SIZE_B       4
#define OPINFO_SIZE_C       4
#define OPINFO_SIZE_A       4
#define OPINFO_SIZE_FMT     2

#define OPINFO_OFFSET_B     (OPINFO_OFFSET_C + OPINFO_SIZE_C)
#define OPINFO_OFFSET_C     (OPINFO_OFFSET_A + OPINFO_SIZE_A)
#define OPINFO_OFFSET_A     (OPINFO_OFFSET_FMT + OPINFO_SIZE_FMT)
#define OPINFO_OFFSET_FMT   0

#define OPINFO_MASK1_B       BITMASK1(OPINFO_SIZE_B)
#define OPINFO_MASK1_C       BITMASK1(OPINFO_SIZE_C)
#define OPINFO_MASK1_A       BITMASK1(OPINFO_SIZE_A)
#define OPINFO_MASK1_FMT     BITMASK1(OPINFO_SIZE_FMT)


/**
 * @details 2025-06-10
 *    +----------+----------+----------+----------+
 *    |  13..10  |  09..06  |  05..02  |  01..00  |
 *    +----------+----------+----------+----------+
 *    | OpArg(B) | OpArg(B) | OpArg(A) | OpFormat |
 *    +----------+----------+----------+----------+
 */
typedef uint16_t OpInfo;

extern const char *const opcode_names[OPCODE_COUNT];
extern const OpInfo opcode_info[OPCODE_COUNT];

#define OPINFO_FMT(op)  (opcode_info[op] >> OPINFO_OFFSET_FMT) & OPINFO_MASK1_FMT
#define OPINFO_A(op)    (opcode_info[op] >> OPINFO_OFFSET_A) & OPINFO_MASK1_A
#define OPINFO_B(op)    (opcode_info[op] >> OPINFO_OFFSET_B) & OPINFO_MASK1_B
#define OPINFO_C(op)    (opcode_info[op] >> OPINFO_OFFSET_C) & OPINFO_MASK1_C

static inline Instruction
instruction_abc(OpCode op, uint8_t a, uint16_t b, uint16_t c)
{
    return (cast(Instruction,  c) << OPCODE_OFFSET_C)
        |  (cast(Instruction,  b) << OPCODE_OFFSET_B)
        |  (cast(Instruction,  a) << OPCODE_OFFSET_A)
        |  (cast(Instruction, op) << OPCODE_OFFSET_OP);
}

static inline Instruction
instruction_abx(OpCode op, uint8_t a, uint32_t bx)
{
    uint16_t b = cast(uint16_t, bx >> OPCODE_SIZE_C); // shift out 'c' bits
    uint16_t c = cast(uint16_t, bx & OPCODE_MAX_C); // mask out 'b' bits
    return instruction_abc(op, a, b, c);
}

static inline bool
rk_is_rk(uint16_t reg)
{
    return reg & OPCODE_BIT_RK;
}

static inline uint16_t
rk_make(uint32_t index)
{
    assert(0 <= index && index <= OPCODE_MAX_RK);
    return cast(uint16_t, index) | OPCODE_BIT_RK;
}

static inline uint16_t
rk_get_k(uint16_t reg)
{
    assert(rk_is_rk(reg));
    return reg & OPCODE_MAX_RK;
}

static inline uint16_t
getarg_c(Instruction i)
{
    return cast(uint16_t, (i >> OPCODE_OFFSET_C) & OPCODE_MAX_C);
}

static inline uint16_t
getarg_b(Instruction i)
{
    return cast(uint16_t, (i >> OPCODE_OFFSET_B) & OPCODE_MAX_B);
}

static inline uint8_t
getarg_a(Instruction i)
{
    return cast(uint8_t, (i >> OPCODE_OFFSET_A) & OPCODE_MAX_A);
}

static inline OpCode
getarg_op(Instruction i)
{
    return cast(OpCode, (i >> OPCODE_OFFSET_OP) & OPCODE_MAX_OP);
}

static inline uint32_t
getarg_bx(Instruction i)
{
    return cast(uint32_t, (i >> OPCODE_OFFSET_BX) & OPCODE_MAX_BX);
}

static inline int32_t
getarg_sbx(Instruction i)
{
    return cast(int32_t, getarg_bx(i)) - OPCODE_MAX_SBX;
}

static inline void
setarg_c(Instruction *ip, uint16_t c)
{
    *ip &= OPCODE_MASK0_OP;
    *ip |= cast(Instruction, c) << OPCODE_OFFSET_C;
}

static inline void
setarg_b(Instruction *ip, uint16_t b)
{
    *ip &= OPCODE_MASK0_B;
    *ip |= cast(Instruction, b) << OPCODE_OFFSET_B;
}

static inline void
setarg_a(Instruction *ip, uint8_t a)
{
    *ip &= OPCODE_MASK0_A;
    *ip |= cast(Instruction, a) << OPCODE_OFFSET_A;
}

static inline void
setarg_bx(Instruction *ip, uint32_t bx)
{
    *ip &= OPCODE_MASK0_BX;
    *ip |= cast(Instruction, bx) << OPCODE_OFFSET_BX;
}

static inline void
setarg_sbx(Instruction *ip, int32_t sbx)
{
    setarg_bx(ip, cast(uint32_t, sbx + OPCODE_MAX_SBX));
}
