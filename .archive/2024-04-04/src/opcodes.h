/* TODO: Refer to https://www.lua.org/source/4.0/lopcodes.h.html */
#ifndef LUA_OPCODES_H
#define LUA_OPCODES_H

#include "lua.h"
#include "limits.h"

/* --- INSTRUCTION FORMAT ------------------------------------------------- {{{1
 
-*- OVERVIEW -------------------------------------------------------------- {{{2

Assumes that all bytecode instructions are unsigned integers.
The first 6 bits (that is, the least significant ones) contain an opcode. All 
instructions must have an opcode. Each instruction can have 0, 1 or 2 operands
which are also known as `arguments`. Depending on their intended argument target
list, instructions can be 1 of 4 types:

Type 0: No Arguments
Type 1: 1 unsigned argument in the higher bits termed 'U'
Type 2: 1 signed argument in the higher bits termed 'S'
Type 3: 1st unsigned argument in the higher bits termed 'A'
        2nd unsigned argument in the middle bits termed 'B'
        
Signed arguments are simply the same bits treated as an unsigned value but
subtracted by the argument's maximum value. Hence, a signed argument of 0 given
a (signed) maximum value of 127 would be interpreted as -127.

-*- 2}}} -----------------------------------------------------------------------

1}}} ------------------------------------------------------------------------ */

/* --- INSTRUCTION POSITIONS/SIZES ---------------------------------------- {{{1
 
Usually, an instruction is 32-bits. U-arguments are 26 bits since they are
defined as `SIZE_INSTRUCTION - SIZE_OPCODE`. In this case B-arguments are 9-bits
and A-arguments are 17-bits.

For simplicity, the following visualizations are in big-endian.
 
Type 0: [  (none): 31..6       ][ OpCode: 5..0 ]
Type 1: [       U: 31..6       ][ OpCode: 5..0 ]
Type 2: [       S: 31..6       ][ OpCode: 5..0 ]
Type 3: [ A: 31..16 ][B: 15..6 ][ OpCode: 5..0 ]

1}}} ------------------------------------------------------------------------ */

enum {
    OpType0,
    OpTypeU,
    OpTypeS,
    OpTypeAB,
};

#define SIZE_INSTRUCTION    (sizeof(Instruction) * CHAR_BIT)
#define SIZE_B              9
#define SIZE_OPCODE         6
#define SIZE_U              (SIZE_INSTRUCTION - SIZE_OPCODE)
#define SIZE_A              (SIZE_INSTRUCTION - POS_A)

#define POS_OPCODE          0
#define POS_U               SIZE_OPCODE
#define POS_B               SIZE_OPCODE
#define POS_A               (SIZE_OPCODE + SIZE_B)

/**
 * @brief   Fill `N` 1-bits in the given bit position range (exclusive):
 *          `[N..N+offset]`.
 * 
 * @note    All other bits are set to 0.
 */
#define MASK1(N, offset)    ((~(LUA_MAXINSTRUCTION << N)) << offset)
#define MASK0(N, offset)    (~MASK1(N, offset))

/* --- VISUALIZATION ------------------------------------------------------ {{{1

MASK1(N = SIZE_OPCODE, offset = 0) => {
    N := 6
    offset := 0

    return ((~((~(Instruction)0) << N)) << offset) => {
        BREAKDOWN:
        1. ((~((~(Instruction)0) << N)) << offset) => {
            Max := (~(Instruction)0)
                | 0b11111111_11111111_11111111_11111111
        }

        2. ((~(Max << N)) << offset) => {
            Fill := (Max << N)
                | 0b11111111_11111111_11111111_11111111 << 6
                | 0b11111111_11111111_11111111_11000000
        }

        3. ((~Fill) << offset) => {
            Flip := (~Fill)
                | ~0b11111111_11111111_11111111_11000000
                | 0b00000000_00000000_00000000_00111111
        }

        4. (Flip << offset) => {
            Final := (Flip << offset)
                | 0b00000000_00000000_00000000_00111111 << 0
                | 0b00000000_00000000_00000000_00111111
        }

        return Final
    }

1}}} ------------------------------------------------------------------------ */

// --- INSTRUCTION MANIPULATION ------------------------------------------- {{{1

static inline int _get_arg(Instruction inst, int pos, int size) {
    int part = inst >> pos;    // Extract the desired section.
    int mask = MASK1(size, 0); // Mask to allow us to throw out other bits.
    return part & mask;        // Get only the bits of the desired section.
}

static inline Instruction _set_arg(Instruction inst, int data, int pos, int size) {
    Instruction extract = inst & MASK0(size, pos);
    Instruction section = cast(Instruction, data) << pos;
    Instruction updated = section & MASK1(size, pos);
    return extract | updated;
}

#define CREATE_0(o)         cast(Instruction, o)
#define GET_OPCODE(inst)    _get_arg(inst, POS_OPCODE, SIZE_OPCODE)
#define SET_OPCODE(inst, opcode) \
    ((inst) = _set_arg(inst, opcode, POS_OPCODE, SIZE_OPCODE))

#define CREATE_U(o, u)      cast(Instruction, o) | (cast(Instruction, u) << POS_U)
#define GETARG_U(inst)      _get_arg(inst, POS_U, SIZE_U)
#define SETARG_U(inst, u)   ((inst) = _set_arg(inst, u, POS_U, SIZE_U))

// 1}}} ------------------------------------------------------------------------

typedef enum {
    OP_RETURN,
} OpCode;

#endif /* LUA_OPCODES_H */
