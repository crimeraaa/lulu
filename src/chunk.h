#ifndef LUA_CHUNK_H
#define LUA_CHUNK_H

#include "conf.h"
#include "lua.h"

/* Must be an unsigned 32-bit integer to fit registers A-C and an opcode. */
typedef LUAI_UINT32 Instruction;
typedef struct Chunk Chunk;

struct Chunk {
    Instruction *code; // 1D array of bytecode.
    int len; // Number of individual `Byte` elements written.
    int cap; // Total useable/writeable number of `Byte` elements.
};

void init_chunk(Chunk *self);
void free_chunk(Chunk *self);
void write_chunk(Chunk *self, Instruction instruction);

void disassemble_chunk(Chunk *self, const char *name);
int disassemble_instruction(Chunk *self, int offset);

#endif /* LUA_CHUNK_H */
