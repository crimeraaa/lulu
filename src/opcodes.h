#ifndef LUA_OPCODES_H
#define LUA_OPCODES_H

#include "lua.h"
#include "limits.h"

/* --- INSTRUCTION FORMAT -----------------------------------------------*- {{{1

The official Lua C implementation assumes each bytecode instruction fits in an
unsigned number. For our purposes we assume it is a `Byte3`. There are several
possible instruction formats:

-*- VISUALIZATION: ------------------------------------------------------*- {{{2
SIZE:   |      9-BIT     |     9-BIT      |      8-BIT     |     6-BIT      |
INDEX:  | [31........24] | [23........15] | [14........06] | [5..........0] |
        |----------------|----------------|----------------|----------------|
iABC:   |   REGISTER B   |   REGISTER C   |   REGISTER A   |     OPCODE     |
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

Note that Register B is the more significant byte than Register C.
- This allows us to use Register B as MSB of Bx, where we treat Bx as unsigned.
- Both Register B and Register C are treated as unsigned 9-bit integers.
- May also be the upper half of an unsigned/signed 18-bit integer.
- Register B and Register C use their 9th bit when they are a constant index.
- That is, it is toggled when they are an index to a value in a constants array.

If Register B is 9 bits, Reigster C takes up the next 9 most significant bits.
Otherwise, Register C is the 9 least significant bits of some 18-bit integer.

-*- 2}}} ---------------------------------------------------------------------*-

-*- LINKS: --------------------------------------------------------------*- {{{2

- https://www.lua.org/source/5.1/lopcodes.h.html
- https://poga.github.io/lua53-notes/bytecode.html
- https://the-ravi-programming-language.readthedocs.io/en/latest/lua_bytecode_reference.html#

-*- 2}}} ---------------------------------------------------------------------*-

1}}} ------------------------------------------------------------------------ */

// Basic instruction format: https://www.lua.org/source/5.1/lopcodes.h.html#OpMode
enum OpMode {iABC, iABx, iAsBx};

// --- REGISTER BIT SIZES ------------------------------------------------- {{{1

#define SIZE_OPCODE         6
#define SIZE_RA             8
#define SIZE_RB             9
#define SIZE_RC             9
#define SIZE_RBx            (SIZE_RB + SIZE_RC)

// 1}}} ------------------------------------------------------------------------

// --- REGISTER BIT POSITIONS --------------------------------------------- {{{1
// Note that in terms of bit position, Register B is more significant than C.

#define POS_OPCODE          0
#define POS_RA              (POS_OPCODE + SIZE_OPCODE)
#define POS_RC              (POS_RA + SIZE_RA)
#define POS_RB              (POS_RC + SIZE_RC)
#define POS_RBx             POS_RC

// 1}}} ------------------------------------------------------------------------

// --- REGISTER MAX VALUES ------------------------------------------------ {{{1

#define MAX_OPCODE          NUM_OPCODES
#define MAXARG_RA           ((1 << SIZE_RA) - 1)
#define MAXARG_RB           ((1 << SIZE_RB) - 1)
#define MAXARG_RC           ((1 << SIZE_RC) - 1)

/**
 * @brief   Maximum allowble values for combined registers. Note that we use
 *          signed int to manipulate the arguments.
 *
 * @note    Assumes that arguments fit in (sizeof(int) * CHAR_BIT) - 1 bits,
 *          where we reserve 1 bit for the sign.
 */
#define MAXARG_RBx          ((1 << SIZE_RBx) - 1)
#define MAXARG_RsBx         (MAXARG_RBx >> 1)

// 1}}} ------------------------------------------------------------------------

// --- INSTRUCTION MANIPULATION ------------------------------------------- {{{1

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

/* --- VISUALIZATION ------------------------------------------------------ {{{2

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

2}}} ------------------------------------------------------------------------ */

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

// --- GET REGISTERS ------------------------------------------------------ {{{2

// Helper function to extract specific parts of the instruction. This could be
// implemented as a function-like macro but I find that very unreadable.
static inline int _get_register(Instruction instruction, int pos, int size) {
    int part = instruction >> pos; // Extract the desired segment using `pos`.
    int mask = MASK1(size, 0);     // Extract segment value only.
    return part & mask;
}

#define GETARG_RA(instruction) \
    _get_register(instruction, POS_RA, SIZE_RA)

#define GETARG_RB(instruction) \
    _get_register(instruction, POS_RB, SIZE_RB)

#define GETARG_RC(instruction) \
    _get_register(instruction, POS_RC, SIZE_RC)

#define GETARG_RBx(instruction) \
    _get_register(instruction, POS_RBx, SIZE_RBx)

#define GETARG_RsBx(instruction) \
    (GETARG_RBx(instruction) - MAXARG_RsBx)

// 2}}} ------------------------------------------------------------------------

// --- SET REGISTERS ------------------------------------------------------ {{{2

#define _set_register(instruction, data, pos, size)                            \
    ((instruction) = (                                                         \
        ((instruction) & MASK0(size, pos))                                     \
        | ((cast(Instruction, data) << pos) & MASK1(size, pos))                \
    ))

#define SETARG_RA(instruction, data) \
    _set_register(instruction, data, POS_RA, SIZE_RA)

#define SETARG_RB(instruction, data) \
    _set_register(instruction, data, POS_RB, SIZE_RB)

#define SETARG_RC(instruction, data) \
    _set_register(instruction, data, POS_RC, SIZE_RC)

#define SETARG_RBx(instruction, data) \
    _set_register(instruction, data, POS_RBx, SIZE_RBx)

#define SETARG_RsBx(instruction, data) \
    SETARG_RBx(instruction, cast(unsigned int, (data) + MAXARG_RsBx))

// 2}}} ------------------------------------------------------------------------

// This is a bit: 0 == is a register, 1 == is a constant.
#define BITRK       (1 << (SIZE_RB - 1))

// Test whether a value is a constant.
#define ISK(x)      ((x) & BITRK)

// Get an index of the constant from the given register.
#define INDEXK(r)   ((int)(r) & ~BITRK)
#define MAXINDEXRK  (BITRK - 1)

// Code a constant index as an RK value.
#define RKASK(x)    ((x) | BITRK)

// Invalid register that fits in 8 bits.
#define NO_REG      MAXARG_RA

// 1}}} ------------------------------------------------------------------------

/**
 * Some Terms:
 * R(x)   := register with index `x`.
 * Kst(x) := constant value from constants table `Kst`.
 * RK(x)  := if `x` is a constant index, use `Kst(INDEXK(x))` else use `R(x)`.
 */
typedef enum {
    /* -------------------------------------------------------------------------
    NAME            ARGS            DESCRIPTION
    ------------------------------------------------------------------------- */
    OP_CONSTANT, // A Bx            R(A) := Kst(Bx)
    OP_ADD,      // A B C           R(A) := RK(B) + RK(C)
    OP_SUB,      // A B C           R(A) := RK(B) - RK(C)
    OP_MUL,      // A B C           R(A) := RK(B) * RK(C)
    OP_DIV,      // A B C           R(A) := RK(B) / RK(C)
    OP_MOD,      // A B C           R(A) := RK(B) % RK(C)
    OP_POW,      // A B C           R(A) := RK(B) ^ RK(C)
    OP_UNM,      // A B             R(A) := -R(B)
    OP_RETURN,   // A B             return R(A), ..., R(A + B - 2)   (See NOTES)
    NUM_OPCODES, // Not a real opcode.
} OpCode;

/* --- NOTES -------------------------------------------------------------- {{{1
OP_RETURN:  A is the index of the first return value.
            B is the number of return values.
            C is unused.
            If B == 0 then no value is returned. 
1}}} ------------------------------------------------------------------------ */

static inline Instruction _make_iABC(OpCode op, int ra, int rb, int rc) {
    Instruction i_op = cast(Instruction, op) << POS_OPCODE;
    Instruction i_ra = cast(Instruction, ra) << POS_RA;
    Instruction i_rb = cast(Instruction, rb) << POS_RB;
    Instruction i_rc = cast(Instruction, rc) << POS_RC;
    return i_rb | i_rc | i_ra | i_op;
}

static inline Instruction _make_iABx(OpCode op, int ra, int rbx) {
    Instruction i_op  = cast(Instruction, op)  << POS_OPCODE;
    Instruction i_ra  = cast(Instruction, ra)  << POS_RA;
    Instruction i_rbx = cast(Instruction, rbx) << POS_RBx;
    return i_rbx | i_ra | i_op;
}

#define CREATE_ABC(opcode, ra, rb, rc)  _make_iABC(opcode, ra, rb, rc)
#define CREATE_ABx(opcode, ra, rbx)     _make_iABx(opcode, ra, rbx)

/**
 * MASKS FOR INSTRUCTION PROPERTIES
 *
 * bits 0-1:   opmode/instruction format: iABC, iABx, iAsBx
 * bits 2-3:   C arg mode: used/unused, register/jump offset, constant index
 * bits 4-5:   B arg mode: used/unused, register/jump offset, constant index
 * bit 6:      instruction set register A is used or not.
 * bit 7:      operator is a test or not.
 *
 * Note that we use a bitmask of `3` (`0b100`) to extract modes with 3 options. 
 */
enum OpArgMask {
    OpArgN, // Unused argument.
    OpArgU, // Used argument.
    OpArgR, // Argument is a register or a jump offset.
    OpArgK, // Argument is a constant index, may be used for RK instructions.
};

/**
 * @brief   Each opcode maps to an opmode which lists particular behaviors.
 *          Specifically, we can tell if a register is used or not, and if
 *          is used then we can also determine if they as indexes to VM
 *          registers or constants.
 *
 * @note    See: https://www.lua.org/source/5.1/lopcodes.c.html#luaP_opmodes
 */
LUA_API const Byte luaP_opmodes[NUM_OPCODES];
LUA_API const char *const luaP_opnames[NUM_OPCODES + 1];

#define get_opname(opcode)  luaP_opnames[opcode]
#define get_OpMode(opcode)  (cast(enum OpMode, luaP_opmodes[opcode] & 3))
#define get_BMode(opcode)   (cast(enum OpArgMask, (luaP_opmodes[opcode] >> 4) & 3))
#define get_CMode(opcode)   (cast(enum OpArgMask, (luaP_opmodes[opcode] >> 2) & 3))
#define test_AMode(opcode)  (luaP_opmodes[opcode] & (1 << 6))
#define test_TMode(opcode)  (luaP_opmodes[opcode] & (1 << 7))

#endif /* LUA_OPCODES_H */
