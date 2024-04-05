#ifndef LULU_CHUNK_H
#define LULU_CHUNK_H

#include "lulu.h"
#include "limits.h"
#include "object.h"

/* See: https://www.lua.org/source/4.0/lopcodes.h.html */
typedef enum {
    /* ----------+--------+----------------+---------------+-------------------|
    |  NAME      |  ARGS  |  STACK BEFORE  |  STACK AFTER  |  SIDE EFFECTS     |
    -------------+--------+----------------+---------------+----------------- */
    OP_CONSTANT, // Bx    |  -             | Constants[Bx] |                   |
    OP_ADD,      // -     |  y, x          | x + y         |                   |
    OP_SUB,      // -     |  y, x          | x - y         |                   |
    OP_MUL,      // -     |  y, x          | x * y         |                   |
    OP_DIV,      // -     |  y, x          | x / y         |                   |
    OP_MOD,      // -     |  y, x          | x % y         |                   |
    OP_POW,      // -     |  y, x          | x ^ y         |                   |
    OP_UNM,      // -     |  x             | -x            |                   |
    OP_RETURN,   // -     |  -             |               |                   |
} OpCode;

// Please keep this up to date accordingly!
#define NUM_OPCODES     (OP_RETURN + 1)

// Lookup table. Maps `OpCode` to `const char*`.
extern const char *const LULU_OPNAMES[];

#define get_opname(opcode)  LULU_OPNAMES[opcode]

/* BYTECODE INSTRUCTIONS -------------------------------------------------- {{{1

The following is taken from Lua 4.0, not Lua 5.1 This is because Lua 5.1 is
a register-based virtual machine and the way they manipulate their instructions
is vastly different than a purely stack-based virtual machine.

-*- OVERVIEW -------------------------------------------------------------- {{{2

Instructions are represented as a single 32-bit unsigned integer, however using
C bitfields we can pack all this information into a struct for easier access.

In the actual Lua C implementation they actually use a plain old integer type
along with a ton of macros to get the bit fiddling done. It's very messy but it
gets the job done.

-*- VISUALIZATION --------------------------------------------------------- {{{3

Let us assume a Big-endian represenation. For convenience I will be following
the naming convention used by Lua 5.1, but the actual usage will reflect the one
from Lua 4.0. So instead of Argument 'U' I will call it Argument 'Bx' simply
because that's what I got used to at this point.

Bit Index:      [ 31................15 ][ 14.......6 ][ 5....0 ]
No Arguments:   [                Unused              ][ OpCode ]
1 (Unsigned):   [              Argument Bx           ][ Opcode ]
1 (Signed):     [              Argument sBx          ][ OpCode ]
2 Arguments:    [      Argument A      ][ Argument B ][ Opcode ]

Argument Bx and sBx use the exact same register and actually carry the exact
same bits. The only difference is, in order to treat sBx as signed while not
changing the bit representation we simply use the maximum positive signed value
as an offset. 

When setting sBx we use +MAXARG_sBx, when getting sBx we use -MAXARG_sBx. This
is a different way of representing negative values than Two's Complement.

-*- 3}}} -----------------------------------------------------------------------

See: https://www.lua.org/source/4.0/lopcodes.h.html

-*- 2}}} -------------------------------------------------------------------- */

#define SIZE_INSTRUCTION    bitsize(uint32_t)
#define SIZEARG_OP          6
#define SIZEARG_Bx          (SIZE_INSTRUCTION - SIZEARG_OP)
#define SIZEARG_B           9
#define SIZEARG_A           (SIZEARG_Bx - SIZEARG_B)

#define MAXARG_Bx           ((1 << SIZEARG_Bx) - 1)
#define MAXARG_B            ((1 << SIZEARG_B)  - 1)
#define MAXARG_A            ((1 << SIZEARG_A)  - 1)
#define MAXARG_sBx          (MAXARG_Bx >> 1)

typedef struct {
    unsigned int upper  : SIZEARG_A;  // Argument A or upper bits of (s)Bx.
    unsigned int lower  : SIZEARG_B;  // Argument B or lower bits of (s)Bx.
    OpCode opcode       : SIZEARG_OP;
} Instruction;

#define getarg_op(inst)     ((inst).opcode)
#define getarg_A(inst)      ((inst).upper)
#define getarg_B(inst)      ((inst).lower)
#define getarg_Bx(inst)     ((getarg_A(inst) << SIZEARG_B) | getarg_B(inst))
#define getarg_sBx(inst)    (getarg_Bx(inst) - MAXARG_sBx)

#define setarg_op(inst, op) ((inst).opcode = (op))
#define setarg_A(inst, a)   ((inst).upper = (a))
#define setarg_B(inst, b)   ((inst).lower = (b))
#define setarg_Bx(inst, bx) \
    (setarg_A(inst, (bx) >> SIZEARG_Bx), setarg_B(inst, (bx) & MAXARG_B))
#define setarg_sBx(inst, bx)    setarg_Bx(inst, (bx) + MAXARG_sBx)

/**
 * @brief   Instructions that require only 1 unsigned operand. Particularly nice
 *          for `OP_CONSTANT` as having the full range of the operand grants us
 *          a lot of possible indexes for constant values.
 */
static inline Instruction create_iBx(OpCode op, int bx) {
    Instruction inst;
    setarg_op(inst, op);
    setarg_Bx(inst, bx);
    return inst;
}

/**
 * @brief   Like `iBx` but for instructions that require only 1 signed operand.
 *          Useful for instructions like `OP_JUMP` which may jump forward (such
 *          as in conditionals, breaks) or backward (loops).
 */
static inline Instruction create_isBx(OpCode op, int sbx) {
    Instruction inst;
    setarg_op(inst, op);
    setarg_sBx(inst, sbx);
    return inst;
}

static inline Instruction create_iAB(OpCode op, int a, int b) {
    Instruction inst;
    setarg_op(inst, op);
    setarg_A(inst, a);
    setarg_B(inst, b);
    return inst;
}

/**
 * @brief   Some arguments don't have their own operands, like `OP_ADD` and 
 *          friends because their operands are already on the stack.
 */
static inline Instruction create_iNone(OpCode op) {
    return create_iAB(op, 0, 0);
}
    
// 1}}} ------------------------------------------------------------------------

typedef struct {
    const char *name;
    TArray constants;
    Instruction *code;
    int *lines; // Mirrors the bytecode array.
    int len;
    int cap;
} Chunk;

void init_chunk(Chunk *self, const char *name);
void free_chunk(Chunk *self);

// Append `data` to the bytecode array.
void write_chunk(Chunk *self, Instruction data, int line);

// Append `value` to the constants array and return its index.
int add_constant(Chunk *self, const TValue *value);

#endif /* LULU_CHUNK_H */
