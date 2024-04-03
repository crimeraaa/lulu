#ifndef LUA_DEBUG_H
#define LUA_DEBUG_H

#include "lua.h"
#include "chunk.h"

void disassemble_chunk(Chunk *self);
int disassemble_instruction(Chunk *self, int offset);

#endif /* LUA_DEBUG_H */
