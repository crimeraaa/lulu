#pragma once

#include "private.h"

enum OpCode {
    OP_LOAD_CONSTANT,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_POW,
    OP_RETURN,
};

static constexpr int OPCODE_COUNT = OP_RETURN + 1;

static constexpr unsigned int
// Operand bit sizes
OPCODE_SIZE_B  = 9,
OPCODE_SIZE_C  = 9,
OPCODE_SIZE_A  = 8,
OPCODE_SIZE_OP = 6,
OPCODE_SIZE_BX = (OPCODE_SIZE_B + OPCODE_SIZE_C),

// RK bit manipulation
OPCODE_BIT_RK   = (1 << (OPCODE_SIZE_B - 1)),
OPCODE_MAX_RK   = (OPCODE_BIT_RK - 1),

// Starting bit indexes
OPCODE_OFFSET_OP =  0,
OPCODE_OFFSET_A  =  (OPCODE_OFFSET_OP + OPCODE_SIZE_OP),
OPCODE_OFFSET_C  =  (OPCODE_OFFSET_A + OPCODE_SIZE_A),
OPCODE_OFFSET_B  =  (OPCODE_OFFSET_C + OPCODE_SIZE_C),
OPCODE_OFFSET_BX =  OPCODE_OFFSET_C;

/**
 * @details 2025-06-10
 *  +--------+--------+--------+--------+
 *  | 31..23 | 22..14 | 13..06 | 05:00  |
 *  +--------+--------+--------+--------+
 *  | Arg(B) | Arg(C) | Arg(A) | OpCode |
 *  +--------+--------+--------+--------+
 */
using Instruction = u32;

// Fills the `n` lower bits with 1's.
// Useful when reading bit fields.
static constexpr Instruction
BITMASK1(int n)
{
    return (1 << (n)) - 1;
}

// Set `start` up to `start + n` bits with 0's. All the rest are 1's.
// Useful when setting bit fields.
static constexpr Instruction
BITMASK0(int start, int n)
{
    return ~(BITMASK1(start) << (n));
}

static constexpr Instruction
OPCODE_MAX_B   = BITMASK1(OPCODE_SIZE_B),
OPCODE_MAX_C   = BITMASK1(OPCODE_SIZE_C),
OPCODE_MAX_A   = BITMASK1(OPCODE_SIZE_A),
OPCODE_MAX_OP  = BITMASK1(OPCODE_SIZE_OP),

OPCODE_MASK0_B  = BITMASK0(OPCODE_SIZE_B, OPCODE_OFFSET_B),
OPCODE_MASK0_C  = BITMASK0(OPCODE_SIZE_C, OPCODE_OFFSET_C),
OPCODE_MASK0_A  = BITMASK0(OPCODE_SIZE_A, OPCODE_OFFSET_A),
OPCODE_MASK0_OP = BITMASK0(OPCODE_SIZE_OP, OPCODE_OFFSET_OP),
OPCODE_MASK0_BX = BITMASK0(OPCODE_SIZE_BX, OPCODE_OFFSET_BX);

static constexpr u32 OPCODE_MAX_BX  = BITMASK1(OPCODE_SIZE_BX);
static constexpr i32 OPCODE_MAX_SBX = cast(i32, OPCODE_MAX_BX >> 1);

enum OpFormat : u16 {
    OPFORMAT_ABC,
    OPFORMAT_ABX,
    OPFORMAT_ASBX,
};

// A bit space inefficient but I find this easier to reason about.
enum OpArg : u16 {
    OPARG_UNUSED,       // 0b0000
    OPARG_REG           = 1 << 0, // 0b0001: Is a register only
    OPARG_CONSTANT      = 1 << 1, // 0b0010: Is a constant index only
    OPARG_REG_CONSTANT,           // 0b0011: Is a register OR a constant
    OPARG_JUMP          = 1 << 2, // 0b0100
    OPARG_TEST,                   // 0b1000
    OPARG_ARGC,                   // 0b1100
};

/**
 * @details 2025-06-10
 *    +----------+----------+----------+----------+
 *    |  13..10  |  09..06  |  05..02  |  01..00  |
 *    +----------+----------+----------+----------+
 *    | OpArg(B) | OpArg(B) | OpArg(A) | OpFormat |
 *    +----------+----------+----------+----------+
 */
using OpInfo = u16;

static constexpr OpInfo
OPINFO_SIZE_B       = 4,
OPINFO_SIZE_C       = 4,
OPINFO_SIZE_A       = 4,
OPINFO_SIZE_FMT     = 2,

OPINFO_OFFSET_FMT   = 0,
OPINFO_OFFSET_A     = (OPINFO_OFFSET_FMT + OPINFO_SIZE_FMT),
OPINFO_OFFSET_C     = (OPINFO_OFFSET_A + OPINFO_SIZE_A),
OPINFO_OFFSET_B     = (OPINFO_OFFSET_C + OPINFO_SIZE_C),

OPINFO_MASK1_B      = BITMASK1(OPINFO_SIZE_B),
OPINFO_MASK1_C      = BITMASK1(OPINFO_SIZE_C),
OPINFO_MASK1_A      = BITMASK1(OPINFO_SIZE_A),
OPINFO_MASK1_FMT    = BITMASK1(OPINFO_SIZE_FMT);

extern const char *const opcode_names[OPCODE_COUNT];
extern const OpInfo opcode_info[OPCODE_COUNT];

static inline OpFormat
opinfo_fmt(OpCode op)
{
    return cast(OpFormat, (opcode_info[op] >> OPINFO_OFFSET_FMT) & OPINFO_MASK1_FMT);
}

static inline OpArg
opinfo_a(OpCode op)
{
    return cast(OpArg, (opcode_info[op] >> OPINFO_OFFSET_A) & OPINFO_MASK1_A);
}

static inline OpArg
opinfo_b(OpCode op)
{
    return cast(OpArg, (opcode_info[op] >> OPINFO_OFFSET_B) & OPINFO_MASK1_B);
}

static inline OpArg
opinfo_c(OpCode op)
{
    return cast(OpArg, (opcode_info[op] >> OPINFO_OFFSET_C) & OPINFO_MASK1_C);
}

static inline Instruction
instruction_abc(OpCode op, u8 a, u16 b, u16 c)
{
    return (cast(Instruction,  c) << OPCODE_OFFSET_C)
        |  (cast(Instruction,  b) << OPCODE_OFFSET_B)
        |  (cast(Instruction,  a) << OPCODE_OFFSET_A)
        |  (cast(Instruction, op) << OPCODE_OFFSET_OP);
}

static inline Instruction
instruction_abx(OpCode op, u8 a, u32 bx)
{
    return instruction_abc(op, a,
        cast(u16, bx >> OPCODE_SIZE_C), // shift out 'c' bits
        cast(u16, bx & OPCODE_MAX_C));  // mask out 'b' bits
}

static inline bool
reg_is_rk(u16 reg)
{
    return reg & OPCODE_BIT_RK;
}

static inline u16
reg_to_rk(u32 index)
{
    lulu_assert(0 <= index && index <= OPCODE_MAX_RK);
    return cast(u16, index) | OPCODE_BIT_RK;
}

static inline u16
reg_get_k(u16 reg)
{
    lulu_assert(reg_is_rk(reg));
    return reg & OPCODE_MAX_RK;
}

static inline u16
getarg_c(Instruction i)
{
    return cast(u16, (i >> OPCODE_OFFSET_C) & OPCODE_MAX_C);
}

static inline u16
getarg_b(Instruction i)
{
    return cast(u16, (i >> OPCODE_OFFSET_B) & OPCODE_MAX_B);
}

static inline u8
getarg_a(Instruction i)
{
    return cast(u8, (i >> OPCODE_OFFSET_A) & OPCODE_MAX_A);
}

static inline OpCode
getarg_op(Instruction i)
{
    return cast(OpCode, (i >> OPCODE_OFFSET_OP) & OPCODE_MAX_OP);
}

static inline u32
getarg_bx(Instruction i)
{
    return cast(u32, (i >> OPCODE_OFFSET_BX) & OPCODE_MAX_BX);
}

static inline i32
getarg_sbx(Instruction i)
{
    return cast(i32, getarg_bx(i)) - OPCODE_MAX_SBX;
}

static inline void
setarg_c(Instruction *ip, u16 c)
{
    *ip &= OPCODE_MASK0_OP;
    *ip |= cast(Instruction, c) << OPCODE_OFFSET_C;
}

static inline void
setarg_b(Instruction *ip, u16 b)
{
    *ip &= OPCODE_MASK0_B;
    *ip |= cast(Instruction, b) << OPCODE_OFFSET_B;
}

static inline void
setarg_a(Instruction *ip, u8 a)
{
    *ip &= OPCODE_MASK0_A;
    *ip |= cast(Instruction, a) << OPCODE_OFFSET_A;
}

static inline void
setarg_bx(Instruction *ip, u32 index)
{
    *ip &= OPCODE_MASK0_BX;
    *ip |= cast(Instruction, index) << OPCODE_OFFSET_BX;
}

static inline void
setarg_sbx(Instruction *ip, i32 jump)
{
    setarg_bx(ip, cast(u32, jump + OPCODE_MAX_SBX));
}
