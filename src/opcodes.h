#ifndef LUA_OPCODES_H
#define LUA_OPCODES_H

#include "lua.h"
#include "limits.h"

/* --- INSTRUCTION FORMAT ------------------------------------------------*- {{{

The official Lua C implementation assumes each bytecode instruction fits in an
unsigned number. For our purposes we assume it is a `Byte3`. There are several
possible instruction formats:

-*- VISUALIZATION: -------------------------------------------------------*- {{{
SIZE:   |      9-BIT     |     9-BIT      |      8-BIT     |     6-BIT      |
INDEX:  |     [31:24]    |    [23:15]     |     [14:06]    |     [5:0]      |
        |----------------|----------------|----------------|----------------|
iABC:   |   REGISTER C   |   REGISTER B   |   REGISTER A   |     OPCODE     |
iABx:   |           REGISTER Bx           |   REGISTER A   |     OPCODE     |
iAsBx:  |           REGISTER sBx          |   REGISTER A   |     OPCODE     |
iAx:    |                    REGISTER Ax                   |     OPCODE     |

The least significant 6 bits are always used for the opcode itself.
- This gives us up to 64 possible opcodes. With Lua's simplicity it is possible
- to stay within this range most of the time.

The 8 bits to the left of the opcode are Register A.
- This is just an index into the `lua_State` (the VM) stack.
- Much of the complexity comes from how we manage and manipulate the stack.
- At the same time we need to be keeping these registers in mind.
- Register A ALWAYS refers to a register, never an index to a constant.

Depending on the instruction format, the next 9 bits may be Register B.
- Is treated as an unsigned 9-bit integer.
- May also be the upper half of an unsigned/signed 18-bit integer.
- Register B and Register C use their 9th bit when they are a constant index.
- That is, it is toggled when they are an index to a value in a constants array.

If Register B is 9 bits, Reigster C takes up the next 9 most significant bits.
Otherwise, Register C is the 9 least significant bits of some 18-bit integer.

-*- }}} ----------------------------------------------------------------------*-

-*- LINKS: ---------------------------------------------------------------*- {{{

- https://www.lua.org/source/5.1/lopcodes.h.html
- https://poga.github.io/lua53-notes/bytecode.html
- https://the-ravi-programming-language.readthedocs.io/en/latest/lua_bytecode_reference.html#

-*- }}} ----------------------------------------------------------------------*-

}}} ------------------------------------------------------------------------- */

// Basic instruction format: https://www.lua.org/source/5.1/lopcodes.h.html#OpMode
enum OpMode {iABC, iABx, iAsBx};

/* --- REGISTER BIT SIZES ----------------------------------------------- {{{ */

#define SIZE_OPCODE         6
#define SIZE_REGISTER_A     8
#define SIZE_REGISTER_B     9
#define SIZE_REGISTER_C     9
#define SIZE_REGISTER_Bx    (SIZE_REGISTER_B + SIZE_REGISTER_C)

/* }}} ---------------------------------------------------------------------- */

/* --- REGISTER BIT POSITIONS ------------------------------------------- {{{ */

#define POS_OPCODE          0
#define POS_REGISTER_A      (POS_OPCODE + SIZE_OPCODE)
#define POS_REGISTER_C      (POS_REGISTER_A + SIZE_REGISTER_A)
#define POS_REGISTER_B      (POS_REGISTER_C + SIZE_REGISTER_C)
#define POS_REGISTER_Bx     POS_REGISTER_C

/* }}} ---------------------------------------------------------------------- */

/* --- REGISTER MAX VALUES ---------------------------------------------- {{{ */

#define MAX_OPCODE          NUM_OPCODES
#define MAX_REGISTER_A      ((1 << SIZE_REGISTER_A) - 1)
#define MAX_REGISTER_B      ((1 << SIZE_REGISTER_B) - 1)
#define MAX_REGISTER_C      ((1 << SIZE_REGISTER_C) - 1)

/**
 * @brief   Maximum allowble values for combined registers. Note that we use
 *          signed int to manipulate the arguments.
 *
 * @note    Assumes that arguments fit in (sizeof(int) * CHAR_BIT) - 1 bits,
 *          where we reserve 1 bit for the sign.
 */
#define MAX_REGISTER_Bx     ((1 << SIZE_REGISTER_Bx) - 1)
#define MAX_REGISTER_sBx    (MAX_REGISTER_Bx >> 1)

/* }}} --------------------------------------------------------------------*- */

/* --- INSTRUCTION MANIPULATION ----------------------------------------- {{{ */

/**
 * @brief   Fill `N` 1-bits in exclusive bit position range `[N : N + offset]`.
 *          All remaining bits are set to 0.
 */
#define MASK1(N, offset)       ((~(LUA_MAXINSTRUCTION << N)) << offset)

/**
 * @brief   Fill `N` 0-bits in exclusive bit position range `[N : N + offset]`.
 *          All remaining bits are set to 1.
 */
#define MASK0(N, offset)       (~MASK1(N, offset))

/* --- VISUALIZATION ------------------------------------------------------- {{{

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

}}} ------------------------------------------------------------------------- */

// Given an instruction, extract the exact value of the `OpCode` portion.
#define GET_OPCODE(instruction)                                                \
    (cast(OpCode, (instruction) >> POS_OPCODE)                                 \
    & MASK1(SIZE_OPCODE, 0))

/**
 * @brief   Adjust the instruction's `OpCode` portion without affecting any
 *          other part. See details for more information on how this works.
 *
 * @details 1. ((instruction) & MASK0(SIZE_OPCODE, POS_OPCODE))
 *             a. Get a bitmask where there are 0-bits at the bit positions
 *                `POS_OPCODE` up to and excluding `POS_OPCODE + SIZE_OPCODE`.
 *             a. Bitwise AND will zero out dissimilar bits, thus "removing" the
 *                opcode portion from this instruction value.
 *
 *          2. ((cast(Instruction, opcode) << POS_OPCODE)
 *             & MASK1(SIZE_OPCODE, POS_OPCODE))
 *             a. Create a temporary `Instruction` using given `opcode`.
 *             b. Left shift by `POS_OPCODE` to move the opcode to the correct
 *                position in case `POS_OPCODE` is greater than 0.
 *             c. Get a bitmask where there are 1-bits at the bit positions
 *                `POS_OPCODE` up to and excluding`POS_OPCODE + SIZE_OPCODE`.
 *             d. Bitwise AND the left-shifted value with the bitmask to create
 *                a 0-filled `Instruction` with only the `OpCode` portion set.
 *
 *          3. (#1) | (#2)
 *             a. Bitwise OR the results to create an `Instruction` that is the
 *                same as the parameter, but with a different opcode.
 *
 *          4. (instruction) = (#3)
 *             b. Assign the given instruction to the new value.
 */
#define SET_OPCODE(instruction, opcode)                                        \
    ((instruction) = (                                                         \
        ((instruction) & MASK0(SIZE_OPCODE, POS_OPCODE))                       \
        | ((cast(Instruction, opcode) << POS_OPCODE)                           \
            & MASK1(SIZE_OPCODE, POS_OPCODE))                                  \
    ))

/* --- GET REGISTERS ---------------------------------------------------- {{{ */

#define GET_REGISTER(instruction, pos, size)                                   \
    (cast(int, (instruction) >> pos)                                           \
    & MASK1(size, 0))

#define GET_REGISTER_A(instruction) \
    (cast(int, (instruction) >> POS_REGISTER_A) \
    & MASK1(SIZE_REGISTER_A, 0))

#define GET_REGISTER_B(instruction) \
    (cast(int, (instruction) >> POS_REGISTER_B) \
    & MASK1(SIZE_REGISTER_B, 0))

#define GET_REGISTER_C(instruction) \
    (cast(int, (instruction) >> POS_REGISTER_C) \
    & MASK1(SIZE_REGISTER_C, 0))    

#define GET_REGISTER_Bx(instruction) \
    (cast(int, (instruction) >> POS_REGISTER_Bx) \
    & MASK1(SIZE_REGISTER_Bx, 0))

#define GET_REGISTER_sBx(instruction) \
    (GET_REGISTER_Bx(instruction) - MAX_REGISTER_sBx)

/* }}} ---------------------------------------------------------------------- */

/* --- SET REGISTERS ---------------------------------------------------- {{{ */

#define SET_REGISTER(instruction, data, pos, size)                             \
    ((instruction) = (                                                         \
        ((instruction) & MASK0(size, pos))                                     \
        | ((cast(Instruction, data) << pos) & MASK1(size, pos))                \
    ))

#define SET_REGISTER_A(instruction, data) \
    ((instruction) = ( \
        ((instruction) & MASK0(SIZE_REGISTER_A, POS_REGISTER_A)) \
        | ((cast(Instruction, data) << POS_REGISTER_A) \
            & MASK1(SIZE_REGISTER_A, POS_REGISTER_A)) \
    ))

#define SET_REGISTER_B(instruction, data) \
    ((instruction) = ( \
        ((instruction) & MASK0(SIZE_REGISTER_B, POS_REGISTER_B)) \
        | ((cast(Instruction, data) << POS_REGISTER_B) \
            & MASK1(SIZE_REGISTER_B, POS_REGISTER_B)) \
    ))

#define SET_REGISTER_C(instruction, data) \
    ((instruction) = ( \
        ((instruction) & MASK0(SIZE_REGISTER_C, POS_REGISTER_C)) \
        | ((cast(Instruction, data) << POS_REGISTER_C) \
            & MASK1(SIZE_REGISTER_C, POS_REGISTER_C)) \
    ))

#define SET_REGISTER_Bx(instruction, data) \
    ((instruction) = ( \
        ((instruction) & MASK0(SIZE_REGISTER_Bx, POS_REGISTER_Bx)) \
        | ((cast(Instruction, data) << POS_REGISTER_Bx) \
            & MASK1(SIZE_REGISTER_Bx, POS_REGISTER_Bx)) \
    ))

#define SET_REGISTER_sBx(instruction, data) \
    SET_REGISTER_Bx(instruction, cast(unsigned int, (data) + MAX_REGISTER_sBx))

/* }}} ---------------------------------------------------------------------- */

#define CREATE_ABC(opcode, a, b, c) \
    ((cast(Instruction, opcode) << POS_OPCODE) \
    | (cast(Instruction, a) << POS_REGISTER_A) \
    | (cast(Instruction, b) << POS_REGISTER_B) \
    | (cast(Instruction, c) << POS_REGISTER_C))

#define CREATE_ABx(opcode, a, bc) \
    ((cast(Instruction, opcode) << POS_OPCODE) \
    | (cast(Instruction, a) << POS_REGISTER_A) \
    | (cast(Instruction, bc) << POS_REGISTER_Bx))

// This is a bit: 0 == is a register, 1 == is a constant.
#define BITRK       (1 << (SIZE_REGISTER_B - 1))

// Test whether a value is a constant.
#define ISK(x)      ((x) & BITRK)

// Get an index of the constant from the given register.
#define INDEXK(r)   ((int)(r) & ~BITRK)
#define MAXINDEXRK  (BITRK - 1)

// Code a constant index as an RK value.
#define RK_AS_K(x)  ((x) | BITRK)

// Invalid register that fits in 8 bits.
#define NO_REG      MAX_REGISTER_A

/* }}} ---------------------------------------------------------------------- */

typedef enum {
    /* -------------------------------------------------------------------------
    NAME            ARGS            DESCRIPTION
    ------------------------------------------------------------------------- */
    OP_CONSTANT, // A Bx            R(A) := Constants(Bx)
    OP_RETURN,
    NUM_OPCODES, // Not a real opcode.
} OpCode;

/* MASKS FOR INSTRUCTION PROPERTIES
bits 0-1:   opmode
bits 2-3:   C arg mode
bits 4-5:   B arg mode
bit 6:      instruction set register A
bit 7:      operator is a test */
enum OpArgMask {
    OpArgN, // Unused argument.
    OpArgU, // Used argument.
    OpArgR, // Argument is a register or a jump offset.
    OpArgK, // Argument is a constant index.
};

extern const Byte luaP_opmodes[NUM_OPCODES];
extern const char *const luaP_opnames[NUM_OPCODES + 1];

#define get_OpMode(mode)   (cast(enum OpMode, luaP_opmodes[mode] & 3))
#define get_BMode(mode)    (cast(enum OpArgMask, (luaP_opmodes[mode] >> 4) & 3))
#define get_CMode(mode)    (cast(enum OpArgMask, (luaP_opmodes[mode] >> 2) & 3))
#define test_AMode(mode)   (luaP_opmodes[mode] & (1 << 6))
#define test_TMode(mode)   (luaP_opmodes[mode] & (1 << 7))

#endif /* LUA_OPCODES_H */
