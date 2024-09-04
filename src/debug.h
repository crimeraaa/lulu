#ifndef LULU_DEBUG_H
#define LULU_DEBUG_H

#include "lulu.h"
#include "chunk.h"

void lulu_Debug_disasssemble_chunk(const lulu_Chunk *self, cstring name);
isize lulu_Debug_disassemble_instruction(const lulu_Chunk *self, isize index);
void lulu_Debug_print_value(const lulu_Value *self);

#endif // LULU_DEBUG_H
