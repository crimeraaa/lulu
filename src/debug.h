#ifndef LULU_DEBUG_H
#define LULU_DEBUG_H

#include "lulu.h"
#include "chunk.h"

void lulu_Debug_print_value(const lulu_Value *value);
void lulu_Debug_disasssemble_chunk(const lulu_Chunk *chunk, cstring name);
isize lulu_Debug_disassemble_instruction(const lulu_Chunk *chunk, isize index);

#endif // LULU_DEBUG_H
