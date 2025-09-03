#pragma once

#include "private.hpp"

enum OpCode : u8 {
    //           | Arguments | Effects
    OP_MOVE,        // A B   | R(A) := R(B)
    OP_CONSTANT,    // A Bx  | R(A) := K[Bx]
    OP_NIL,         // A B   | R(i) := nil for A <= i <= B
    OP_BOOL,        // A B C | R(A) := Bool(B); if Bool(C) then ip++
    OP_GET_GLOBAL,  // A Bx  | R(A) := _G[K(Bx)]
    OP_SET_GLOBAL,  // A Bx  | _G[K(Bx)] := R(A)
    OP_NEW_TABLE,   // A B C | R(A) := {} ; #hash = B, #array = C
    OP_GET_TABLE,   // A B C | R(A) := R(B)[RK(C)]
    OP_SET_TABLE,   // A B C | R(A)[RK(B)] := RK(C)
    OP_SET_ARRAY,   // A B C | R(A)[C*FPF + i] := R(A+i) for 1 <= i <= B
    OP_GET_UPVALUE, // A B   | R(A) := Upvalues[B]
    OP_SET_UPVALUE, // A B   | Upvalues[B] := R(A)
    OP_ADD,         // A B C | R(A) := RK(B) + RK(C)
    OP_SUB,         // A B C | R(A) := RK(B) - RK(C)
    OP_MUL,         // A B C | R(A) := RK(B) * RK(C)
    OP_DIV,         // A B C | R(A) := RK(B) / RK(C)
    OP_MOD,         // A B C | R(A) := RK(B) % RK(C)
    OP_POW,         // A B C | R(A) := RK(B) ^ RK(C)
    OP_EQ,          // A B C | if ((RK(B) == RK(C)) != Bool(A)) then ip++
    OP_LT,          // A B C | if ((RK(B) <  RK(C)) != Bool(A)) then ip++
    OP_LEQ,         // A B C | if ((RK(B) <= RK(C)) != Bool(A)) then ip++
    OP_UNM,         // A B   | R(A) := -R(B)
    OP_NOT,         // A B   | R(A) := not R(B)
    OP_LEN,         // A B   | R(A) := len(R(B))
    OP_CONCAT,      // A B C | R(A) := concat(R(B:C))
    OP_TEST,        // A   C | if Bool(R(A)) == Bool(C) then ip++
    OP_TEST_SET,    // A B C | if Bool(R(B)) == Bool(C)
                    //         then R(A) := R(B)
                    //         else ip++
    OP_JUMP,        // sBx   | ip += sBx
    OP_FOR_PREP,    // A sBx | R(A) -= R(A+2) ; ip += sBx
    OP_FOR_LOOP,    // A sBx | R(A) += R(A+2) ; if R(A) < R(A+1)
                    //                          then ip += sBx
                    //                        , R(A+3) := R(A)
    OP_FOR_IN,      // A   C | R(A+3:A+3+C) := R(A)(R(A+1), R(A+2));
                    //       | if R(A+3) != nil then R(A+2) := R(A+3)
                    //       | else ip++
    OP_CALL,        // A B C | R(A:A+C) := R(A)(R(A+1:A+B+1))
    OP_SELF,        // A B C | R(A+1) := R(B); R(A) := R(B)[RK(C)]
    OP_CLOSURE,     // A Bx  | R(A) := Chunks[Bx]
    OP_CLOSE,       // A     | close R(0:A+1)
    OP_RETURN,      // A B   | return R(A:A+B)
};

// To avoid too much stack usage, we separate calls to `OP_SET_ARRAY` by
// every nth element.
#define FIELDS_PER_FLUSH 50

constexpr int OPCODE_COUNT = OP_RETURN + 1;

// Fills the `n` lower bits with 1's.
// Useful when reading bit fields.
constexpr u32
BIT_MASK1(int n)
{
    return (1 << static_cast<u32>(n)) - 1;
}

// Set `start` up to `start + n` bits with 0's. All the rest are 1's.
// Useful when setting bit fields.
constexpr u32
BIT_MASK0(int start, int n)
{
    return ~(BIT_MASK1(start) << (n));
}

/** @details 2025-06-10
 *  +--------+--------+--------+--------+
 *  | 31..23 | 22..14 | 13..06 | 05:00  |
 *  +--------+--------+--------+--------+
 *  | Arg(B) | Arg(C) | Arg(A) | OpCode |
 *  +--------+--------+--------+--------+
 */
struct Instruction {
    u32 value;

    // clang-format off
    static constexpr u32
    // Operand bit sizes
    SIZE_B    = 9,
    SIZE_C    = 9,
    SIZE_A    = 8,
    SIZE_OP   = 6,
    SIZE_BX   = SIZE_B + SIZE_C,

    // RK bit manipulation
    BIT_RK    = 1 << (SIZE_B - 1),
    MAX_RK    = BIT_RK - 1,

    // Starting bit indexes
    OFFSET_OP = 0,
    OFFSET_A  = OFFSET_OP + SIZE_OP,
    OFFSET_C  = OFFSET_A + SIZE_A,
    OFFSET_B  = OFFSET_C + SIZE_C,
    OFFSET_BX = OFFSET_C;

    static constexpr u32
    // Operand limits
    MAX_B    = BIT_MASK1(SIZE_B),
    MAX_C    = BIT_MASK1(SIZE_C),
    MAX_A    = BIT_MASK1(SIZE_A),
    MAX_OP   = BIT_MASK1(SIZE_OP),

    // Operand masks
    MASK0_B  = BIT_MASK0(SIZE_B, OFFSET_B),
    MASK0_C  = BIT_MASK0(SIZE_C, OFFSET_C),
    MASK0_A  = BIT_MASK0(SIZE_A, OFFSET_A),
    MASK0_OP = BIT_MASK0(SIZE_OP, OFFSET_OP),
    MASK0_BX = BIT_MASK0(SIZE_BX, OFFSET_BX);

    static constexpr u32 MAX_BX  = BIT_MASK1(SIZE_BX);
    static constexpr i32 MAX_SBX = static_cast<i32>(MAX_BX >> 1);
    // clang-format on

    static constexpr Instruction
    make_abc(OpCode op, u16 a, u16 b, u16 c)
    {
        return {
            (static_cast<u32>(b) << OFFSET_B)
            | (static_cast<u32>(c) << OFFSET_C)
            | (static_cast<u32>(a) << OFFSET_A)
            | (static_cast<u32>(op) << OFFSET_OP)};
    }

    static constexpr Instruction
    make_abx(OpCode op, u16 a, u32 bx)
    {
        u16 b = static_cast<u16>(bx >> SIZE_C); // shift out `c` bits
        u16 c = static_cast<u16>(bx & MAX_C);   // mask out `b` bits
        return make_abc(op, a, b, c);
    }

    static constexpr Instruction
    make_asbx(OpCode op, u16 a, i32 sbx)
    {
        return make_abx(op, a, static_cast<u32>(sbx + MAX_SBX));
    }

    OpCode
    op() const noexcept
    {
        return static_cast<OpCode>(this->extract<OFFSET_OP, MAX_OP>());
    }

    u16
    a() const noexcept
    {
        return static_cast<u16>(this->extract<OFFSET_A, MAX_A>());
    }

    u16
    b() const noexcept
    {
        return static_cast<u16>(this->extract<OFFSET_B, MAX_B>());
    }

    u16
    c() const noexcept
    {
        return static_cast<u16>(this->extract<OFFSET_C, MAX_C>());
    }

    u32
    bx() const noexcept
    {
        return this->extract<OFFSET_BX, MAX_BX>();
    }

    i32
    sbx() const noexcept
    {
        // NOTE(2025-07-16): We assume the cast is safe because [s]bx is a
        // i24/u24, so all valid values of either un/signed should fit
        // in even an i32.
        return static_cast<i32>(this->bx()) - MAX_SBX;
    }

    void
    set_a(u16 a) noexcept
    {
        this->set<OFFSET_A, MASK0_A>(static_cast<u32>(a));
    }

    void
    set_b(u16 b) noexcept
    {
        this->set<OFFSET_B, MASK0_B>(static_cast<u32>(b));
    }

    void
    set_c(u16 c) noexcept
    {
        this->set<OFFSET_C, MASK0_C>(static_cast<u32>(c));
    }

    void
    set_bx(u32 bx) noexcept
    {
        this->set<OFFSET_BX, MASK0_BX>(bx);
    }

    void
    set_sbx(i32 sbx) noexcept
    {
        this->set<OFFSET_BX, MASK0_BX>(static_cast<u32>(sbx + MAX_SBX));
    }

    static bool
    reg_is_k(u16 reg)
    {
        return reg & BIT_RK;
    }

    static u16
    reg_to_rk(u32 index)
    {
        return static_cast<u16>(index) | BIT_RK;
    }

    static u16
    reg_get_k(u16 reg)
    {
        return reg & MAX_RK;
    }


private:
    template<u32 OFFSET, u32 MASK1>
    constexpr u32
    extract() const noexcept
    {
        return (this->value >> OFFSET) & MASK1;
    }

    template<u32 OFFSET, u32 MASK0>
    constexpr void
    set(u32 arg) noexcept
    {
        this->value &= MASK0; // Clear previous bits of argument.
        this->value |=
            (arg << OFFSET); // Shift new argument into correct position.
    }
};

enum OpFormat : u8 {
    OPFORMAT_ABC,
    OPFORMAT_ABX,
    OPFORMAT_ASBX,
};

enum OpArg : u8 {
    OPARG_UNUSED, // 0b00
    OPARG_REGK,   // 0b01: Is a register or a constant
    OPARG_JUMP,   // 0b10: Is a jump offset
    OPARG_OTHER,  // 0b11: Used as condition, count or boolean
};

/** @details 2025-06-10
 *    +----------+----------+----------+----------+
 *    | 06..06   |  05..04  |  03..02  |  01..00  |
 *    +----------+----------+----------+----------+
 *    | bool(A)  | OpArg(B) | OpArg(C) | OpFormat |
 *    +----------+----------+----------+----------+
 */
struct OpInfo {
    u8 value;

    // clang-format off
    static constexpr u8
    // Info sizes
    SIZE_TEST = 1,
    SIZE_A    = 1,
    SIZE_B    = 2,
    SIZE_C    = 2,
    SIZE_FMT  = 2,

    // Info bit offsets
    OFFSET_FMT  = 0,
    OFFSET_C    = OFFSET_FMT + SIZE_FMT,
    OFFSET_B    = OFFSET_C + SIZE_C,
    OFFSET_A    = OFFSET_B + SIZE_B,
    OFFSET_TEST = OFFSET_A + SIZE_A,

    // Info bit masks
    MASK1_TEST = BIT_MASK1(SIZE_TEST),
    MASK1_A    = BIT_MASK1(SIZE_A),
    MASK1_B    = BIT_MASK1(SIZE_B),
    MASK1_C    = BIT_MASK1(SIZE_C),
    MASK1_FMT  = BIT_MASK1(SIZE_FMT);
    // clang-format on

    OpFormat
    fmt() const noexcept
    {
        return static_cast<OpFormat>(this->extract<OFFSET_FMT, MASK1_FMT>());
    }

    bool
    test() const noexcept
    {
        return static_cast<bool>(this->extract<OFFSET_TEST, MASK1_TEST>());
    }

    // A is used as a destination register?
    bool
    a() const noexcept
    {
        return static_cast<bool>(this->extract<OFFSET_A, MASK1_A>());
    }

    OpArg
    b() const noexcept
    {
        return static_cast<OpArg>(this->extract<OFFSET_B, MASK1_B>());
    }

    OpArg
    c() const noexcept
    {
        return static_cast<OpArg>(this->extract<OFFSET_C, MASK1_C>());
    }

private:
    template<u8 OFFSET, u8 MASK1>
    u8
    extract() const noexcept
    {
        return (this->value >> OFFSET) & MASK1;
    }
};

/** @note(2025-07-19) ORDER: Keep in sync with `OpCode`! */
LULU_DATA const char *const opnames[OPCODE_COUNT];


/** @note(2025-07-19) ORDER: Keep in sync with `OpCode`! */
LULU_DATA const OpInfo opinfo[OPCODE_COUNT];


/** @brief Pack an integer into an 8-bit 'floating-point byte'.
 *
 * @details
 *  Although not particularly accurate, it allows us to store large sizes.
 *
 *  Format: 0b_eeee_exxx
 *      Where 'e' is the exponent and 'x' is the mantissa.
 */
u16
floating_byte_make(isize x);


/** @brief Decode the integer stored in an 8-bit 'floating-point byte'.
 *
 * @details
 *  Recall the format of an 8-bit floating point byte: `0b_eeee_exxx`
 *  So to decode:
 *      if 0b000e_eeee == 0:
 *          return 0b0000_0xxx
 *      else:
 *          return (0b0000_1xxx) * 2^(0b000e_eeee - 1)
 */
isize
floating_byte_decode(u16 fbyte);
