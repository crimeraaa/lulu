#ifndef LULU_COMPILER_H
#define LULU_COMPILER_H

#include "lexer.h"
#include "chunk.h"

#define UNRESOLVED_LOCAL    (-1)
#define UNINITIALIZED_LOCAL (-1)

/**
 * @brief
 *      (2 ** 24) - 1 = 0b11111111_11111111_11111111
 */
#define LULU_MAX_CONSTANTS  ((1 << 24) - 1)
#define LULU_MAX_LOCALS     LULU_MAX_BYTE

/**
 * @brief
 *      An assignable value, sometimes called an 'L-value'.
 */
typedef struct lulu_Assign lulu_Assign;
struct lulu_Assign {
    lulu_Assign *prev;  // Use recursion to chain multiple assignments.
    lulu_OpCode  op;    // GETGLOBAL, GETLOCAL, or GETTABLE.
    byte3        index; // Argument to 'op'.
};

typedef struct lulu_Parser lulu_Parser;
typedef struct lulu_Compiler lulu_Compiler;

struct lulu_Parser {
    lulu_Token     current;  // Also our "lookahead" token.
    lulu_Token     consumed; // Analogous to the book's `compiler.c:Parser::previous`.
    lulu_Assign   *assignments; // Must be valid only once per assignment call.
    lulu_Compiler *compiler;
    lulu_Lexer    *lexer;
};

typedef struct {
    lulu_String *name;
    int          depth;
} lulu_Local;

struct lulu_Compiler {
    lulu_VM     *vm;    // Enclosing/parent state.
    lulu_Chunk  *chunk; // Destination for bytecode and constants.
    lulu_Parser *parser;
    lulu_Lexer  *lexer;
    lulu_Local   locals[LULU_MAX_LOCALS];
    int          n_locals;
    int          scope_depth; // 0 isn't really used but is distinct from -1.
    int          stack_usage;
};

void
lulu_Compiler_init(lulu_VM *vm, lulu_Compiler *self);

/**
 * @note 2024-10-30
 *      Assumes 'self' has been initialized properly.
 */
void
lulu_Compiler_compile(lulu_Compiler *self, cstring input, lulu_Chunk *chunk);

void
lulu_Compiler_end(lulu_Compiler *self);

void
lulu_Compiler_emit_opcode(lulu_Compiler *self, lulu_OpCode op);

void
lulu_Compiler_emit_return(lulu_Compiler *self);

byte3
lulu_Compiler_make_constant(lulu_Compiler *self, const lulu_Value *value);

void
lulu_Compiler_emit_constant(lulu_Compiler *self, const lulu_Value *value);

void
lulu_Compiler_emit_string(lulu_Compiler *self, const lulu_Token *token);

void
lulu_Compiler_emit_number(lulu_Compiler *self, lulu_Number n);

void
lulu_Compiler_emit_byte1(lulu_Compiler *self, lulu_OpCode op, byte a);

void
lulu_Compiler_emit_byte3(lulu_Compiler *self, lulu_OpCode op, byte3 arg);

void
lulu_Compiler_begin_scope(lulu_Compiler *self);

void
lulu_Compiler_end_scope(lulu_Compiler *self);

/**
 * @note 2024-12-10
 *      (Somewhat) Analogous to `compiler.c:declareVariable()` in the book.
 *      Combines functionality of that and `compiler.c:addLocal()`.
 */
void
lulu_Compiler_add_local(lulu_Compiler *self, const lulu_Token *ident);

/**
 * @note 2024-12-26
 *      Marks all current locals as 'initialized', that is they are now able to
 *      be referenced in their current scope. This was done to allow code like:
 *
 *      x = 10
 *      do
 *          local x = x + 1
 *          -- do stuff with local 'x'
 *      end
 */
void
lulu_Compiler_initialize_locals(lulu_Compiler *self);

/**
 * @note 2024-12-10
 *      Analogous to `compiler.c:resolveLocal()` in the book.
 */
int
lulu_Compiler_resolve_local(lulu_Compiler *self, const lulu_Token *ident);

isize
lulu_Compiler_new_table(lulu_Compiler *self);

void
lulu_Compiler_adjust_table(lulu_Compiler *self, isize i_code, isize n_fields);

void
lulu_Compiler_set_table(lulu_Compiler *self, int i_table, int i_key, int n_pop);

#endif // LULU_COMPILER_H
