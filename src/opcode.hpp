#pragma once

#include "private.hpp"

enum OpCode : u8 {
//           | Arguments | Effects
OP_CONSTANT,    // A Bx  | R(A) := K[Bx]
OP_LOAD_NIL,    // A B   | R(i) := nil for A <= i <= B
OP_LOAD_BOOL,   // A B   | R(A) := Bool(B)
OP_GET_GLOBAL,  // A Bx  | R(A) := _G[K(Bx)]
OP_SET_GLOBAL,  // A Bx  | _G[K(Bx)] := R(A)
OP_NEW_TABLE,   // A B C | R(A) := {} ; #hash = B, #array = C
OP_GET_TABLE,   // A B C | R(A) := R(B)[RK(C)]
OP_SET_TABLE,   // A B C | R(A)[RK(B)] := RK(C)
OP_MOVE,        // A B   | R(A) := R(B)
OP_ADD,         // A B C | R(A) := RK(B) + RK(C)
OP_SUB,         // A B C | R(A) := RK(B) - RK(C)
OP_MUL,         // A B C | R(A) := RK(B) * RK(C)
OP_DIV,         // A B C | R(A) := RK(B) / RK(C)
OP_MOD,         // A B C | R(A) := RK(B) % RK(C)
OP_POW,         // A B C | R(A) := RK(B) ^ RK(C)
OP_EQ,          // A B C | R(A) := RK(B) == RK(C)
OP_LT,          // A B C | R(A) := RK(B) <  RK(C)
OP_LEQ,         // A B C | R(A) := RK(B) <= RK(C)
OP_UNM,         // A B   | R(A) := -R(B)
OP_NOT,         // A B   | R(A) := not R(B)
OP_CONCAT,      // A B C | R(A) := concat(R(B:C))
OP_TEST,        // A   C | if Bool(R(A)) ~= Bool(C) then pc++
OP_JUMP,        // sBx   | ip += sBx
OP_CALL,        // A B C | R(A:A+C) := R(A)(R(A+1:A+B+1))
OP_RETURN,      // A B C | return R(A:A+B)
};

static constexpr int OPCODE_COUNT = OP_RETURN + 1;

static constexpr u32
// Operand bit sizes
OPCODE_SIZE_B  = 9,
OPCODE_SIZE_C  = 9,
OPCODE_SIZE_A  = 8,
OPCODE_SIZE_OP = 6,
OPCODE_SIZE_BX = OPCODE_SIZE_B + OPCODE_SIZE_C,

// RK bit manipulation
OPCODE_BIT_RK   = 1 << (OPCODE_SIZE_B - 1),
OPCODE_MAX_RK   = OPCODE_BIT_RK - 1,

// Starting bit indexes
OPCODE_OFFSET_OP =  0,
OPCODE_OFFSET_A  =  OPCODE_OFFSET_OP + OPCODE_SIZE_OP,
OPCODE_OFFSET_C  =  OPCODE_OFFSET_A + OPCODE_SIZE_A,
OPCODE_OFFSET_B  =  OPCODE_OFFSET_C + OPCODE_SIZE_C,
OPCODE_OFFSET_BX =  OPCODE_OFFSET_C;

/**
 * @details 2025-06-10
 *  +--------+--------+--------+--------+
 *  | 31..23 | 22..14 | 13..06 | 05:00  |
 *  +--------+--------+--------+--------+
 *  | Arg(B) | Arg(C) | Arg(A) | OpCode |
 *  +--------+--------+--------+--------+
 */
struct Instruction {
    u32 value;
};

// Fills the `n` lower bits with 1's.
// Useful when reading bit fields.
LULU_FUNC constexpr u32
BITMASK1(int n)
{
    return (1 << cast(u32)n) - 1;
}

// Set `start` up to `start + n` bits with 0's. All the rest are 1's.
// Useful when setting bit fields.
LULU_FUNC constexpr u32
BITMASK0(int start, int n)
{
    return ~(BITMASK1(start) << (n));
}

static constexpr u32
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
static constexpr i32 OPCODE_MAX_SBX = cast(i32)(OPCODE_MAX_BX >> 1);

enum OpFormat : u16 {
    OPFORMAT_ABC,
    OPFORMAT_ABX,
    OPFORMAT_ASBX,
};

// A bit space inefficient but I find this easier to reason about.
enum OpArg : u16 {
    OPARG_UNUSED,   // 0b0000
    OPARG_REGK,     // 0b0001: Is a register or a constant
    OPARG_JUMP,     // 0b0010: Is a jump offset
    OPARG_OTHER,    // 0b0011: Used as condition, count or boolean
};

/**
 * @details 2025-06-10
 *    +----------+----------+----------+----------+
 *    |  13..10  |  09..06  |  05..02  |  01..00  |
 *    +----------+----------+----------+----------+
 *    | OpArg(B) | OpArg(C) | OpArg(A) | OpFormat |
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

LULU_DATA const char *const
opcode_names[OPCODE_COUNT];

LULU_DATA const OpInfo
opcode_info[OPCODE_COUNT];

LULU_FUNC constexpr OpFormat
opinfo_fmt(OpCode op)
{
    return cast(OpFormat)((opcode_info[op] >> OPINFO_OFFSET_FMT) & OPINFO_MASK1_FMT);
}

LULU_FUNC inline OpArg
opinfo_a(OpCode op)
{
    return cast(OpArg)((opcode_info[op] >> OPINFO_OFFSET_A) & OPINFO_MASK1_A);
}

LULU_FUNC inline OpArg
opinfo_b(OpCode op)
{
    return cast(OpArg)((opcode_info[op] >> OPINFO_OFFSET_B) & OPINFO_MASK1_B);
}

LULU_FUNC inline OpArg
opinfo_c(OpCode op)
{
    return cast(OpArg)((opcode_info[op] >> OPINFO_OFFSET_C) & OPINFO_MASK1_C);
}

LULU_FUNC constexpr Instruction
instruction_abc(OpCode op, u16 a, u16 b, u16 c)
{
    return {(cast(u32)c  << OPCODE_OFFSET_C)
        |   (cast(u32)b  << OPCODE_OFFSET_B)
        |   (cast(u32)a  << OPCODE_OFFSET_A)
        |   (cast(u32)op << OPCODE_OFFSET_OP)};
}

LULU_FUNC constexpr Instruction
instruction_abx(OpCode op, u16 a, u32 bx)
{
    u16 b = cast(u16)(bx >> OPCODE_SIZE_C); // shift out 'c' bits
    u16 c = cast(u16)(bx & OPCODE_MAX_C);   // mask out 'b' bits
    return instruction_abc(op, a, b, c);
}

LULU_FUNC constexpr Instruction
instruction_asbx(OpCode op, u16 a, i32 bx)
{
    return instruction_abx(op, a, cast(u32)(bx + OPCODE_MAX_SBX));
}

LULU_FUNC constexpr bool
reg_is_rk(u16 reg)
{
    return reg & OPCODE_BIT_RK;
}

LULU_FUNC constexpr u16
reg_to_rk(u32 index)
{
    return cast(u16)index | OPCODE_BIT_RK;
}

LULU_FUNC constexpr u16
reg_get_k(u16 reg)
{
    return reg & OPCODE_MAX_RK;
}

LULU_FUNC constexpr u16
getarg_c(Instruction i)
{
    return cast(u16)((i.value >> OPCODE_OFFSET_C) & OPCODE_MAX_C);
}

LULU_FUNC constexpr u16
getarg_b(Instruction i)
{
    return cast(u16)((i.value >> OPCODE_OFFSET_B) & OPCODE_MAX_B);
}

LULU_FUNC constexpr u16
getarg_a(Instruction i)
{
    return cast(u16)((i.value >> OPCODE_OFFSET_A) & OPCODE_MAX_A);
}

LULU_FUNC constexpr OpCode
getarg_op(Instruction i)
{
    return cast(OpCode)((i.value >> OPCODE_OFFSET_OP) & OPCODE_MAX_OP);
}

LULU_FUNC constexpr u32
getarg_bx(Instruction i)
{
    return cast(u32)((i.value >> OPCODE_OFFSET_BX) & OPCODE_MAX_BX);
}

LULU_FUNC constexpr i32
getarg_sbx(Instruction i)
{
    // We assume the cast is safe, because `[s]Bx` arguments are only 24 bits.
    return cast(i32)getarg_bx(i) - OPCODE_MAX_SBX;
}

LULU_FUNC inline void
setarg_c(Instruction *ip, u16 c)
{
    ip->value &= OPCODE_MASK0_C;
    ip->value |= cast(u32)c << OPCODE_OFFSET_C;
}

LULU_FUNC inline void
setarg_b(Instruction *ip, u16 b)
{
    ip->value &= OPCODE_MASK0_B;
    ip->value |= cast(u32)b << OPCODE_OFFSET_B;
}

LULU_FUNC inline void
setarg_a(Instruction *ip, u16 a)
{
    ip->value &= OPCODE_MASK0_A;
    ip->value |= cast(u32)a << OPCODE_OFFSET_A;
}

LULU_FUNC inline void
setarg_bx(Instruction *ip, u32 index)
{
    ip->value &= OPCODE_MASK0_BX;
    ip->value |= cast(u32)index << OPCODE_OFFSET_BX;
}

LULU_FUNC inline void
setarg_sbx(Instruction *ip, i32 jump)
{
    setarg_bx(ip, cast(u32)(jump + OPCODE_MAX_SBX));
}
