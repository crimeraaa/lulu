#ifndef LULU_OPCODES_H
#define LULU_OPCODES_H

#include "common.h"

typedef enum {
    OP_RETURN,
} OpCode;

#define LULU_OPSIZE_NONE    (0)
#define LULU_OPSIZE_BYTE    (1)
#define LULU_OPSIZE_BYTE2   (2)
#define LULU_OPSIZE_BYTE3   (3)
#define LULU_OPSIZE_BYTE4   (4)

/**
 * @brief       Get the next index into the bytecode array which represents the
 *              next opcode, skipping any of the previous opcode's operands;
 *              
 *              Determine by the operand size of the current opcode.
 *              Opcodes can have no operand (thus 0 bytes) or up to 4 bytes,
 *              which will be unmasked create a 32-bit operand.
 *
 * @param i     Some index into a Chunk's bytecode array.
 * @param sz    Some offset to add to `i`, pass one of `LULU_OPSIZE*` macros.
 */
#define LULU_OPCODE_NEXT(i, sz) (i) + (sz)

#endif /* LULU_OPCODES_H */
