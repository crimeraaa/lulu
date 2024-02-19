#ifndef LUA_CHUNK_H
#define LUA_CHUNK_H

#include "common.h"

typedef enum {
    OP_RETURN,
} LuaOpCode;

typedef struct {
    LuaOpCode *code; // 1D array of 8-bit instructions.
    int count;       // Current number of instructions written to `code`.
    int capacity;    // Total number of instructions we can hold currently.
} LuaChunk;

void init_chunk(LuaChunk *self);
void deinit_chunk(LuaChunk *self);
void write_chunk(LuaChunk *self, uint8_t byte);

/**
 * Mainly for debug purposes only.
 */
void disassemble_chunk(LuaChunk *self, const char *name);
int disassemble_instruction(LuaChunk *self, int offset);

#endif /* LUA_CHUNK_H */
