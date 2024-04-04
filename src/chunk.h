#ifndef LULU_CHUNK_H
#define LULU_CHUNK_H

#include "conf.h"
#include "object.h"

typedef enum {
    OP_CONSTANT,
    OP_RETURN,
} OpCode;

// Please keep this up to date accordingly!
#define NUM_OPCODES         (OP_RETURN + 1)
#define get_opname(opcode)  OPNAMES[opcode]

extern const char *const OPNAMES[];

typedef struct {
    const char *name;
    TArray constants;
    Byte *code;
    int *lines; // Mirrors the bytecode array.
    int len;
    int cap;
} Chunk;

void init_chunk(Chunk *self, const char *name);
void free_chunk(Chunk *self);

// Append `data` to the bytecode array.
void write_chunk(Chunk *self, Byte data, int line);

// Append `value` to the constants array and return its index.
int add_constant(Chunk *self, const TValue *value);

#endif /* LULU_CHUNK_H */
