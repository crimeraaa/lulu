#ifndef LUA_CHUNK_H
#define LUA_CHUNK_H

#include "common.h"
#include "value.h"

/* Until this limit is reached, we use `OP_CONSTANT` which takes a 1-byte operand. */
#define MAX_CONSTANTS_SHORT     (UINT8_MAX)
/** 
 * If MAX_CONSTANTS_SHORT has been surpassed, we use `OP_CONSTANT_LONG` and
 * supply it with a 3-byte (24-bit) operand.
 */
#define MAX_CONSTANTS_LONG      (1 << 24)

typedef enum {
    OP_CONSTANT, // Load constant value into memory using an 8-bit operand.

    // -*- III:14.2(CHALLENGE): Use a 24-bit operand. ------------------------*-

    OP_CONSTANT_LONG, // Load a constant value into memory using a 24-bit operand.

    // -*- III:15.3:    An Arithmetic calculator -----------------------------*-

    OP_UNM, // Unary negation, a.k.a. "Unary minus" (hence "UNM").
    
    // -*- III:15.3.1:  Binary Operators -------------------------------------*-

    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV, 
    OP_MOD, // My addition for modulo using the caret ('%') character.
    OP_POW, // My addition for exponentiation using the caret ('^') character.

    OP_RET,
} LuaOpCode;

typedef struct {
    int where; // Line number for run-length-encoding.
    int start; // First instruction's byte offset from the `code` array.
    int end; // Last instruction's byte offset from the `code` array.
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
    uint8_t *code; // 1D array of 8-bit instructions.
    int count;     // Current number of instructions written to `code`.
    int capacity;  // Total number of instructions we can hold currently.
    int prevline;  // Track the previous line number as we may skip some.
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
 * Challenge III:14.1
 * 
 * As much as we can, we want to use the byte-sized OP_CONSTANT function when
 * loading constant values since we assume most of our instructions are 1 byte.
 * 
 * However, 1 byte instructions mean that when we load an index, we can only use
 * up to 255 as that's the simple maximum number of constants in the pool.
 *
 * If one wishes to have more constants, you'll need to use OP_CONSTANT_LONG.
 * The instruction itself takes 1 byte, but it takes a 3 byte (or 24-bit) operand.
 * 
 * III:17.4.1   Parsers for Tokens
 * 
 * This function has now been replaced by `compiler.c:emit_constant()`.
 */
// void write_constant(LuaChunk *self, LuaValue value, int line);

#ifdef DEBUG_PRINT_CODE

void disassemble_chunk(LuaChunk *self, const char *name);
int disassemble_instruction(LuaChunk *chunk, int offset);

#endif /* DEBUG_PRINT_CODE */

#endif /* LUA_CHUNK_H */
