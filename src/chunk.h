#ifndef LULU_CHUNK_H
#define LULU_CHUNK_H

#include "lulu.h"
#include "limits.h"
#include "object.h"

/**
 * @brief   Conceptually, our stack grows from left to right. Values to the
 *          right are more recent and values to the left are older.
 *
 * @details Glossary:
 *          `B3`    An unsigned `Byte3`, decoded from the 3 bytes following the
 *                  current opcode.
 *          `Kst`   Constants table of the current chunk.
 *          `V_*`   Negative offset relative to the VM's top of the stack.
 *          `-`     When used like `V_B3..-..V_1`, indicates we need to concat
 *                  (`..`) values Stack[-B3] (`V_B3`) all the way up to top of
 *                  the Stack[-1] (`V_1`).
 * @note    See: https://www.lua.org/source/4.0/lopcodes.h.html
 */
typedef enum {
    /* ----------+--------+----------------+------------------+----------------|
    |  NAME      |  ARGS  |  STACK BEFORE  |  STACK AFTER     |  SIDE EFFECTS  |
    -------------+--------+----------------+------------------+---------------*/
    OP_CONSTANT, // B3    |  -             | Kst[B3]          |                |
    OP_NIL,      // -     |  -             | nil              |                |
    OP_TRUE,     // -     |  -             | true             |                |
    OP_FALSE,    // -     |  -             | false            |                |
    OP_EQ,       // -     |  x, y          | x == y           |                |
    OP_LT,       // -     |  x, y          | x < y            |                |
    OP_LE,       // -     |  x, y          | x <= y           |                |
    OP_ADD,      // -     |  x, y          | x + y            |                |
    OP_SUB,      // -     |  x, y          | x - y            |                |
    OP_MUL,      // -     |  x, y          | x * y            |                |
    OP_DIV,      // -     |  x, y          | x / y            |                |
    OP_MOD,      // -     |  x, y          | x % y            |                |
    OP_POW,      // -     |  x, y          | x ^ y            |                |
    OP_CONCAT,   // B3    |  V_B3,..,V_1   | V_B3..-..V_1     |                |
    OP_NOT,      // -     |  x             | not x            |                |
    OP_UNM,      // -     |  x             | -x               |                |
    OP_RETURN,   // -     |  -             |                  |                |
} OpCode;

// Please keep this up to date accordingly!
#define NUM_OPCODES             (OP_RETURN + 1)

#define encode_byte2_msb(N)     (((N) >> bitsize(Byte)) & MAX_BYTE)
#define encode_byte2_lsb(N)     ((N) & MAX_BYTE)
#define encode_byte2(N)         (encode_byte2_msb(N)                           \
                                | encode_byte2_lsb(N))
#define decode_byte2(msb, lsb)  (((msb) << bitsize(Byte))                      \
                                | (lsb))

#define encode_byte3_msb(N)     (((N) >> bitsize(Byte2)) & MAX_BYTE)
#define encode_byte3_mid(N)     (((N) >> bitsize(Byte))  & MAX_BYTE)
#define encode_byte3_lsb(N)     ((N) & MAX_BYTE)
#define decode_byte3(msb, mid, lsb) (((msb) << bitsize(Byte2))                 \
                                    | ((mid) << bitsize(Byte))                 \
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
void free_chunk(VM *vm, Chunk *self);

// Append `data` to the bytecode array.
void write_chunk(VM *vm, Chunk *self, Byte data, int line);

// Append `value` to the constants array and return its index.
int add_constant(VM *vm, Chunk *self, const TValue *value);

#endif /* LULU_CHUNK_H */
