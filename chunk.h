#ifndef LUA_CHUNK_H
#define LUA_CHUNK_H

#include "common.h"
#include "value.h"

typedef enum {
    OP_CONSTANT, // Load constant value into memory using an 8-bit operand.
    OP_CONSTANT_LONG, // Challenge III:14.2: Use a 24-bit operand.
    OP_RETURN,
} LuaOpCode;

typedef struct {
    int where; // Line number for run-length-encoding.
    uint8_t start; // First instruction in the `code` array that is on this line.
    uint8_t end; // Last instruction in the `code` array that is on this line.
} LuaLineRun;

/** 
 * Challenge III:14.2: Run Length Encoding
 * 
 * Instead of using a 1D array that parallels the bytecode array, we can be a bit
 * smarter and instead record "runs" of consective line occurences.
 */
typedef struct {
    int count;        // Non-empty linecount. May not line up with line numbers.
    int capacity;     // Number of elements allocated for.
    LuaLineRun *runs; // 1D array of information about consecutive line sequences.
} LuaLineRLE;

typedef struct {
    LuaValueArray constants;
    LuaLineRLE lines;
    LuaOpCode *code; // 1D array of 8-bit instructions.
    int count;       // Current number of instructions written to `code`.
    int capacity;    // Total number of instructions we can hold currently.
    int prevline;    // Track the previous line number as we may skip some.
} LuaChunk;

void init_chunk(LuaChunk *self);
void deinit_chunk(LuaChunk *self);
void write_chunk(LuaChunk *self, uint8_t byte, int line);

/**
 * @brief       Append `value` to the given chunk's constants pool.
 *              Reallocation of arrays may occur.
 * 
 * @return      Index of this value into the constants pool. This return value 
 *              should be emitted as the operand to `OP_CONSTANT`.
 */
int add_constant(LuaChunk *self, LuaValue value);

/**
 * Mainly for debug purposes only.
 */
void disassemble_chunk(LuaChunk *self, const char *name);
int disassemble_instruction(LuaChunk *self, int offset);

#endif /* LUA_CHUNK_H */
