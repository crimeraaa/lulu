#ifndef LULU_OPCODES_H
#define LULU_OPCODES_H

#include "common.h"

/* INSTRUCTION FORMAT ------------------------------------------------------ {{{

-*- OVERVIEW: ----------------------------------------------------------------*-

The official Lua C implementation assumes each bytecode instruction fits in an
unsigned number. For our purposes we assume it is a `Byte3`. There are several
possible instruction formats:

-*- VISUALIZATION: -------------------------------------------------------------

SIZE:   [ 9          ][ 9          ][ 8          ][ 6      ]
INDEX:  [ 31:24      ][ 23:15      ][ 14:06      ][ 5:0    ]
iABC:   [ REGISTER C ][ REGISTER B ][ REGISTER A ][ OPCODE ]
iABx:   [        REGISTER Bx       ][ REGISTER A ][ OPCODE ]
iAsBx:  [        REGISTER sBx      ][ REGISTER A ][ OPCODE ]
iAx:    [                 REGISTER Ax            ][ OPCODE ]

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

If Register B is 9 bits, then Reigster C takes up the next 9 most significant bits.
Otherwise, Register C is the 9 least significant bits of an 18-bit integer of some
signedness.

LINKS:
- https://www.lua.org/source/5.1/lopcodes.h.html
- https://poga.github.io/lua53-notes/bytecode.html 
- https://the-ravi-programming-language.readthedocs.io/en/latest/lua_bytecode_reference.html#
}}} ------------------------------------------------------------------------- */

typedef enum {
    OP_RETURN,
} OpCode;

// Basic instruction format: https://www.lua.org/source/5.1/lopcodes.h.html#OpMode
enum OpMode {iABC, iABx, iAsBx};

/* VISUALIZATION ----------------------------------------------------------- {{{


}}} ------------------------------------------------------------------------- */

#define SIZE_REGISTER_A     (8)
#define SIZE_REGISTER_B     (9)
#define SIZE_REGISTER_C     (9)
#define SIZE_REGISTER_Bx    (SIZE_REGISTER_B + SIZE_REGISTER_C)
#define SIZE_OPCODE         (6)

#define POS_OPCODE          (0)
#define POS_REGISTER_A      (POS_OPCODE + SIZE_OPCODE)
#define POS_REGISTER_C      (POS_REGISTER_A + SIZE_REGISTER_A)
#define POS_REGISTER_B      (POS_REGISTER_C + SIZE_REGISTER_C)

#define OPERAND_NONE        (0)
#define OPERAND_BYTE        (1)
#define OPERAND_BYTE2       (2)
#define OPERAND_BYTE3       (3)
#define OPERAND_BYTE4       (4)

/**
 * @brief       Get the next index into the bytecode array which represents the
 *              next opcode, skipping any of the previous opcode's operands;
 *              
 *              Determine by the operand size of the current opcode.
 *              Opcodes can have no operand (thus 0 bytes) or up to 4 bytes,
 *              which will be unmasked create a 32-bit operand.
 *
 * @param i     Some index into a Chunk's bytecode array.
 * @param sz    Some offset to add to `i`. Pass a `OPERAND_*` macro.
 */
#define next_instruction(i, sz) (i) + (sz)

#endif /* LULU_OPCODES_H */
