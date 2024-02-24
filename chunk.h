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

    // -*- III:14.2:    Use a 24-bit operand (CHALLENGE) ---------------------*-
    OP_CONSTANT_LONG, // Load a constant value into memory using a 24-bit operand.

    // -*- III:18.4:    Two New Types ----------------------------------------*-
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    
    // -*- III:21.1.2   Expression statements --------------------------------*-
    OP_POP,
    
    // -*- III:21.2     Variable Declarations --------------------------------*-
    OP_GET_GLOBAL,
    OP_GET_GLOBAL_LONG,
    OP_DEFINE_GLOBAL,
    OP_DEFINE_GLOBAL_LONG,
     
    // -*- III:18.4.2   Equality and comparison operators --------------------*-
    OP_EQ,
    // OP_NEQ,
    OP_GT,
    // OP_GE,
    OP_LT,
    // OP_LE,
    
    // -*- III:15.3.1:  Binary Operators -------------------------------------*-
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV, 
    OP_MOD, // My addition for modulo using the caret ('%') character.
    OP_POW, // My addition for exponentiation using the caret ('^') character.
    
    // -*- III:18.4.1   Logical not and falseiness ---------------------------*-
    OP_NOT,
    
    // -*- III:19.4.1   Concatentation ---------------------------------------*-
    OP_CONCAT, // My addition for a distinct string concatenation operator.

    // -*- III:15.3:    An Arithmetic calculator -----------------------------*-
    OP_UNM, // Unary negation, a.k.a. "Unary minus" (hence "UNM").

    // -*- III:21.1.1   Print statements -------------------------------------*-
    OP_PRINT,

    OP_RET,
} OpCode;

typedef struct {
    int where; // Line number for run-length-encoding.
    int start; // First instruction's byte offset from the `code` array.
    int end;   // Last instruction's byte offset from the `code` array.
} Linerun;

/** 
 * Challenge III:14.2: Run Length Encoding
 * 
 * Instead of using a 1D array that parallels the bytecode array, we can be a bit
 * smarter and instead record "runs" of consective line occurences.
 */
typedef struct {
    int count;     // Non-empty linecount. May not line up with line numbers.
    int capacity;  // Number of elements allocated for.
    Linerun *runs; // 1D array of information about consecutive line sequences.
} LineRLE;

typedef struct {
    ValueArray constants;
    LineRLE lines;
    Byte *code; // 1D array of 8-bit instructions.
    int count;     // Current number of instructions written to `code`.
    int capacity;  // Total number of instructions we can hold currently.
    int prevline;  // Track the previous line number as we may skip some.
} Chunk;

void init_chunk(Chunk *self);
void free_chunk(Chunk *self);
void write_chunk(Chunk *self, Byte byte, int line);

/**
 * @brief       Append `value` to the given chunk's constants pool.
 *              Reallocation of arrays may occur.
 * 
 * @return      Index of this value into the constants pool. This return value 
 *              should be emitted as the operand to `OP_CONSTANT`.
 */
int add_constant(Chunk *self, TValue value);

/**
 * Challenge III:14.1
 * 
 * In addition to using run-length encoding, we have to be able to somehow query
 * into it using an instruction offset.
 * 
 * Do note that this increments `chunk->prevline` if the previous line does not
 * match the current line at the given bytecode offset.
 */
int get_instruction_line(Chunk *chunk, int offset);

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
// void write_constant(Chunk *self, TValue value, int line);

#ifdef DEBUG_PRINT_CODE

void disassemble_chunk(Chunk *self, const char *name);
int disassemble_instruction(Chunk *chunk, int offset);

#endif /* DEBUG_PRINT_CODE */

#endif /* LUA_CHUNK_H */
