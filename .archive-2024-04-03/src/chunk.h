#ifndef LUA_CHUNK_H
#define LUA_CHUNK_H

#include "conf.h"
#include "lua.h"
#include "limits.h"
#include "object.h"

typedef struct {
    TValue *values;
    int len;
    int cap;
} TArray;

typedef struct Chunk {
    TArray constants; // 1D array of constant values to load at runtime.
    const char *name; // Filename of script, or `"stdin"` if in REPL.
    Instruction *code; // 1D array of bytecode.
    int *lines; // Line information mirrors the bytecode array for simplicity.
    int len; // Current number of individual `Byte` elements written.
    int cap; // Total useable/writeable number of `Byte` elements.
} Chunk;

void init_chunk(Chunk *self, const char *name);
void free_chunk(Chunk *self);
void write_chunk(Chunk *self, Instruction instruction, int line);

// Appends `value` to chunk's constants array and returns index we appended to.
int add_constant(Chunk *self, const TValue *value);

#endif /* LUA_CHUNK_H */
