#pragma once

#include <cstdint>

#include "memory.hpp"

using Value = double;

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;

enum OpCode : u8 {
    OP_LOAD_CONSTANT,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_POW,
    OP_RETURN,
};

/**
 * @details 2025-06-10
 * -    Format:
 *
 */
struct Instruction {
    // Manual bitfield information
    static constexpr unsigned int

    // Manual bitfield sizes
    SIZE_B  = 9,
    SIZE_C  = 9,
    SIZE_A  = 8,
    SIZE_OP = 6,
    SIZE_BX = SIZE_B + SIZE_C,
    
    // 0-based bit indexes of where each argument is in the full integer.
    OFFSET_OP = 0,
    OFFSET_A  = OFFSET_OP + SIZE_OP,
    OFFSET_C  = OFFSET_A  + SIZE_A,
    OFFSET_B  = OFFSET_C  + SIZE_C,
    OFFSET_BX = OFFSET_C,
    
    // After shifting, use these to extract exactly the argument you're after.
    MASK_B    = (1 << SIZE_B)  - 1,
    MASK_C    = (1 << SIZE_C)  - 1,
    MASK_A    = (1 << SIZE_A)  - 1,
    MASK_OP   = (1 << SIZE_OP) - 1,
    MASK_BX   = (1 << SIZE_BX) - 1;
    
    Instruction(OpCode op, u8 a, u16 b, u16 c)
    {
        u32
        ib  = static_cast<u32>(b)  << OFFSET_B,
        ic  = static_cast<u32>(c)  << OFFSET_C,
        ia  = static_cast<u32>(a)  << OFFSET_A,
        iop = static_cast<u32>(op) << OFFSET_OP;

        m_value = (ic | ib | ia | iop);
    }
    
    Instruction(OpCode op)
        : Instruction(op, 0, 0, 0)
    { /* empty */ }

    Instruction(OpCode op, u8 reg, u32 index)
        : Instruction(op, reg, (index >> SIZE_B) & MASK_B, index & MASK_C)
    { /* empty */ }
    
    u16
    b() const noexcept
    {
        return static_cast<u16>((m_value >> OFFSET_B) & MASK_B);
    }
    
    u16
    c() const noexcept
    {
        return static_cast<u16>((m_value >> OFFSET_C) & MASK_C);
    }

    OpCode
    op() const noexcept
    {
        return static_cast<OpCode>((m_value >> OFFSET_OP) & MASK_OP);
    }
    
    u32
    a() const noexcept
    {
        return static_cast<u16>((m_value >> OFFSET_A) & MASK_A);
    }
    
    u32
    bx() const noexcept
    {
        return (m_value >> OFFSET_BX) & MASK_BX;
    }

    u32 m_value;
};

struct Chunk {
    Dynamic<Instruction> code;
    Dynamic<Value>       constants;
    int                  stack_used = 2;
};

void
chunk_append(lulu_VM &vm, Chunk &c, Instruction i);

u32
chunk_add_constant(lulu_VM &vm, Chunk &c, Value v);

void
chunk_destroy(lulu_VM &vm, Chunk &c);

void
chunk_list(const Chunk &c);
