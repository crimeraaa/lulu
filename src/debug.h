#ifndef LULU_DEBUG_H
#define LULU_DEBUG_H

#include "lulu.h"
#include "chunk.h"

// Wrapper to surround strings with quotes for easier identification.
void luluDbg_print_value(const Value *v);
void luluDbg_disassemble_chunk(const Chunk *ck);

/**
 * @brief   Print out the disassembly for the instruction found at the given
 *          `offset` index into the chunk's bytecode array.
 *
 * @note    Assumes that the bytecode at the given `offset` represents a valid
 *          `OpCode`, which in turn should fit in a `Byte`.
 */
int luluDbg_disassemble_instruction(const Chunk *ck, int offset);

#endif /* LULU_DEBUG_H */
