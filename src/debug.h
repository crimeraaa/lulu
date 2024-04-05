#ifndef LULU_DEBUG_H
#define LULU_DEBUG_H

#include "lulu.h"
#include "chunk.h"

void disassemble_chunk(const Chunk *self);

/**
 * @brief   Print out the disassembly for the instruction found at the given
 *          `offset` index into the chunk's bytecode array.
 * 
 * @note    Assumes that the bytecode at the given `offset` represents a valid
 *          `OpCode`, which in turn should fit in a `Byte`.
 */
void disassemble_instruction(const Chunk *self, int offset);

#endif /* LULU_DEBUG_H */
