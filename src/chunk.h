#ifndef LULU_CHUNK_H
#define LULU_CHUNK_H

#include "lulu.h"
#include "limits.h"
#include "object.h"

/* See: https://www.lua.org/source/4.0/lopcodes.h.html */
typedef enum {
    /* ----------+--------+----------------+---------------+-------------------|
    |  NAME      |  ARGS  |  STACK BEFORE  |  STACK AFTER  |  SIDE EFFECTS     |
    -------------+--------+----------------+---------------+----------------- */
    OP_CONSTANT, // Byte3 |  -             | Constants[B3] |                   |
    OP_NIL,      // -     |  -             | nil           |                   |
    OP_TRUE,     // -     |  -             | true          |                   |
    OP_FALSE,    // -     |  -             | false         |                   |
    OP_ADD,      // -     |  y, x          | x + y         |                   |
    OP_SUB,      // -     |  y, x          | x - y         |                   |
    OP_MUL,      // -     |  y, x          | x * y         |                   |
    OP_DIV,      // -     |  y, x          | x / y         |                   |
    OP_MOD,      // -     |  y, x          | x % y         |                   |
    OP_POW,      // -     |  y, x          | x ^ y         |                   |
    OP_NOT,      // -     |  x             | not x         |                   |
    OP_UNM,      // -     |  x             | -x            |                   |
    OP_RETURN,   // -     |  -             |               |                   |
} OpCode;

// Please keep this up to date accordingly!
#define NUM_OPCODES     (OP_RETURN + 1)

#define encode_byte2_msb(N)                 (((N) >> bitsize(Byte)) & MAX_BYTE)
#define encode_byte2_lsb(N)                 ((N) & MAX_BYTE)
#define decode_byte2(msb, lsb)              (((msb) << bitsize(Byte)) \
                                            | (lsb))

#define encode_byte3_msb(N)                 (((N) >> bitsize(Byte2)) & MAX_BYTE)
#define encode_byte3_mid(N)                 (((N) >> bitsize(Byte))  & MAX_BYTE)
#define encode_byte3_lsb(N)                 ((N) & MAX_BYTE)
#define decode_byte3(msb, mid, lsb)         (((msb) << bitsize(Byte2)) \
                                            | ((mid) << bitsize(Byte)) \
                                            | (lsb))

// Lookup table. Maps `OpCode` to `const char*`.
extern const char *const LULU_OPNAMES[];

#define get_opname(opcode)  LULU_OPNAMES[opcode]

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
