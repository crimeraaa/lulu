#ifndef LULU_CHUNK_H
#define LULU_CHUNK_H

#include "common.h"
#include "conf.h"

typedef struct Chunk Chunk;

struct Chunk {
    Byte *code; // 1D array of bytecode.
    int len; // Number of individual `Byte` elements written.
    int cap; // Total useable/writeable number of `Byte` elements.
};

void init_chunk(Chunk *self);
void free_chunk(Chunk *self);
void write_chunk(Chunk *self, Byte byte);

void disassemble_chunk(Chunk *self, const char *name);
int disassemble_instruction(Chunk *self, int offset);

#endif /* LULU_CHUNK_H */
