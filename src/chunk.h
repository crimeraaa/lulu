#ifndef LULU_CHUNK_H
#define LULU_CHUNK_H

#include "lulu.h"
#include "limits.h"
#include "object.h"
#include "memory.h"

/**
 * @brief   Conceptually, our stack grows from left to right. Values to the
 *          right are more recent and values to the left are older.
 *
 * @details Glossary:
 *          L       An unsigned `Byte` representing an index into the current
 *                  stack frame where local variables are found.
 *          B1      A single `Byte` argument immediately following the opcode.
 *          B3      A `Byte3` argument. The MSB is the byte immediately
 *                  following the opcode, then the next, then LSB.
 *          sB3     A `Byte3` argument, however we fake signedness by utilizing
 *                  bit manipulation. Simply put, we treat all sB3's as if they
 *                  were unsigned. However we reserve the uppermost bit (24) as
 *                  the sign bit as in two's complement. We toggle this bit ON
 *                  when the number is negative, otherwise it is positive.
 *                  
 *                  See `chunk.h:(encode|decode)_sbyte3()` macros for more info.
 *
 *          Kst     Constants table of the current chunk.
 *          V_*     Negative offset relative to the VM's top of the stack.
 *          A_*     Not sure how this is different from `V_*`.
 *          -       When used like `V_U..-..V_1`, indicates we need to concat
 *                  (`..`) values Stack[-U] (`V_U`) all the way up to top of
 *                  the Stack[-1] (`V_1`).
 *          _G      Like in base Lua, this is the table where global variables
 *                  can be accessed from and written to.
 * @note    See: https://www.lua.org/source/4.0/lopcodes.h.html
 */
typedef enum {
/* ----------+----------+-----------------------+-------------------------+----------------------|
|  NAME      |  ARGS    |  STACK BEFORE         |  STACK AFTER            |  SIDE EFFECTS        |
-------------+----------+-----------------------+-------------------------+---------------------*/
OP_CONSTANT, // B3      | -                     | Kst[B3]                 |                      |
OP_NIL,      // B1      | -                     | (push B1 nils)          |                      |
OP_TRUE,     // -       | -                     | true                    |                      |
OP_FALSE,    // -       | -                     | false                   |                      |
OP_POP,      // B1      | Top[-B1...-1]         | -                       |                      |
OP_NEWTABLE, // B3      | -                     | {} (size B3)            |                      |
OP_GETLOCAL, // L       | -                     | Loc[L]                  |                      |
OP_GETGLOBAL,// B3      | -                     | _G[Kst[B3]]             |                      |
OP_GETTABLE, // -       | Tbl, Key              | Tbl[Key]                |                      |
OP_SETLOCAL, // L       | x                     | -                       | Loc[L] = x           |
OP_SETGLOBAL,// B3      | x                     | -                       | _G[Kst[B3]] = x      |
OP_SETTABLE, // A, B, C | t, k, ..., v          | (pop C values)          | t[k] = v             |
OP_SETARRAY, // A, B    | t, ...                | (set B values)          | t[1...A] = ...       |
OP_EQ,       // -       | x, y                  | x == y                  |                      |
OP_LT,       // -       | x, y                  | x < y                   |                      |
OP_LE,       // -       | x, y                  | x <= y                  |                      |
OP_ADD,      // -       | x, y                  | x + y                   |                      |
OP_SUB,      // -       | x, y                  | x - y                   |                      |
OP_MUL,      // -       | x, y                  | x * y                   |                      |
OP_DIV,      // -       | x, y                  | x / y                   |                      |
OP_MOD,      // -       | x, y                  | x % y                   |                      |
OP_POW,      // -       | x, y                  | x ^ y                   |                      |
OP_CONCAT,   // B1      | Top[-B1...-1]         | concat(...)             |                      |
OP_UNM,      // -       | x                     | -x                      |                      |
OP_NOT,      // -       | x                     | not x                   |                      |
OP_LEN,      // -       | x                     | #x                      |                      |
OP_PRINT,    // B1      | Top[-B1...-1]         | -                       | print(...)           |
OP_TEST,     // _       | Top[-1]               | Top[-1]                 | if Top[-1] ip++      |
OP_JUMP,     // sB3     | -                     | -                       | ip += sB3            |
OP_RETURN,   // -       | -                     |                         |                      |
} OpCode;

/* -----------------------------------------------------------------------------
OP_SETTABLE:
    ARGUMENT A:
    - An absolute index into the stack where the table is found. We assume that
    the very top of the stack is where the desired value is.

    ARGUMENT B:
    - An absolute index into the stack where the key is found. This should be
    able to support both table constructors and multiple assignments, hence
    we explicitly track it.

    ARGUMENT C:
    - Refers to how many values will be popped by this instruction. Since it's
    a variable delta we cannot afford an implicit pop of the value otherwise the
    compiler will report wrong stack usages. Doing so will also break how table
    fields due to their reliance on `Compiler::stack_usage`.

    - Note that ARGUMENT C can be increased by an OP_POP being optimized.

OP_SETARRAY:
    ARGUMENT A:
    - Similar to OP_SETTABLE, this represents the absolute index into the stack
    where the table is found.

    ARGUMENT B:
    - Refers to how many values from the top of the stack are to be assigned to
    the array portion of table referred to by ARGUMENT A. In other words, it is
    the number of array elements. Remember that Lua indexes start at 1.

    We achieve this behavior by popping the key-value pair for each non-array
    index element, but let expressions without explicit keys remain until later.

OP_TEST:
    This instruction is used to implement logical and branching statements.
    It always tests for truthiness.

    If Top[-1] == 0 then we move to the next instruction (see ip++).
    Otherwise, we jump over said instruction.

ip++:
    This assumes that the next instruction is an OP_JUMP.
----------------------------------------------------------------------------- */

typedef const struct {
    const char *name;  // String representation sans `"OP_"`.
    int8_t      argsz; // How many bytes the opcode's argument takes up.
    int8_t      push;  // How many stack pushes are made from this operation.
    int8_t      pop;   // How many stack pops are made from this operation.
} OpInfo;

// Indicates number of pushes/pops is determined on a case-by-case basis. e.g.
// `OP_NIL` may push anywhere from 1 to `MAX_BYTE` values.
#define VAR_DELTA (-1)

// Please keep this up to date accordingly!
#define NUM_OPCODES             (OP_RETURN + 1)

#define decode_byte2_msb(N)     (((N) >> bit_size(Byte)) & MAX_BYTE)
#define decode_byte2_lsb(N)     ((N) & MAX_BYTE)
#define decode_byte2(N)         (decode_byte2_msb(N) | decode_byte2_lsb(N))
#define encode_byte2(msb, lsb)  (((msb) << bit_size(Byte)) | (lsb))

#define decode_byte3_msb(N)     (((N) >> bit_size(Byte2)) & MAX_BYTE)
#define decode_byte3_mid(N)     (((N) >> bit_size(Byte))  & MAX_BYTE)
#define decode_byte3_lsb(N)     ((N) & MAX_BYTE)
#define decode_byte3(N)         (decode_byte3_msb(N) \
                                | decode_byte3_mid(N) \
                                | decode_byte3_lsb(N))

#define encode_byte3(msb, mid, lsb) (((msb) << bit_size(Byte2))                 \
                                    | ((mid) << bit_size(Byte))                 \
                                    | (lsb))

#define encode_sbyte3(b3)       ((b3) | MIN_SBYTE3)
#define check_sbyte3(b3)        ((b3) & MIN_SBYTE3)
#define decode_sbyte3(b3)       ((b3) & ~MIN_SBYTE3)

// Lookup table which maps `OpCode` to its respective `OpInfo` struct.
extern OpInfo LULU_OPINFO[];

#define get_opinfo(op)  LULU_OPINFO[op]
#define get_opsize(op)  (get_opinfo(op).argsz + 1)

typedef struct {
    Table       mappings; // Map values to indexes into the `constants` array.
    Array       constants;
    const char *name;
    Byte       *code;
    int        *lines; // Mirrors the bytecode array.
    int         len;
    int         cap;
} Chunk;

void luluFun_init_chunk(Chunk *ck, const char *name);
void luluFun_free_chunk(lulu_VM *vm, Chunk *ck);

// Append `data` to the bytecode array.
void luluFun_write_chunk(lulu_VM *vm, Chunk *ck, Byte data, int line);

// Append `value` to the constants array and return its index.
int luluFun_add_constant(lulu_VM *vm, Chunk *ck, const Value *v);

#endif /* LULU_CHUNK_H */
