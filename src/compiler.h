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

typedef struct LValue   LValue;
typedef struct Parser   Parser;
typedef struct Compiler Compiler;

struct Parser {
    Token     current;  // Also our "lookahead" token.
    Token     consumed; // Analogous to the book's `compiler.c:Parser::previous`.
    LValue   *lvalues;  // Must be valid only once per assignment call.
    Compiler *compiler;
    Lexer    *lexer;
};

typedef struct {
    OString *name;
    int      depth;
} Local;

struct Compiler {
    lulu_VM *vm;    // Enclosing/parent state.
    Chunk   *chunk; // Destination for bytecode and constants.
    Parser  *parser;
    Lexer   *lexer;
    Local    locals[LULU_MAX_LOCALS];
    int      n_locals;
    int      scope_depth; // 0 isn't really used but is distinct from -1.
    int      stack_usage;
};

void
compiler_init(lulu_VM *vm, Compiler *self);

/**
 * @note 2024-10-30
 *      Assumes 'self' has been initialized properly.
 */
void
compiler_compile(Compiler *self, cstring input, Chunk *chunk);

void
compiler_end(Compiler *self);

void
compiler_emit_opcode(Compiler *self, OpCode op);

void
compiler_emit_return(Compiler *self);

byte3
compiler_make_constant(Compiler *self, const Value *value);

void
compiler_emit_constant(Compiler *self, const Value *value);

void
compiler_emit_string(Compiler *self, const Token *token);

void
compiler_emit_number(Compiler *self, lulu_Number n);

void
compiler_emit_A(Compiler *self, OpCode op, byte a);

void
compiler_emit_byte3(Compiler *self, OpCode op, byte3 arg);

void
compiler_begin_scope(Compiler *self);

void
compiler_end_scope(Compiler *self);

/**
 * @note 2024-12-10
 *      (Somewhat) Analogous to `compiler.c:declareVariable()` in the book.
 *      Combines functionality of that and `compiler.c:addLocal()`.
 */
void
compiler_add_local(Compiler *self, const Token *ident);

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
compiler_initialize_locals(Compiler *self);

/**
 * @note 2024-12-10
 *      Analogous to `compiler.c:resolveLocal()` in the book.
 */
int
compiler_resolve_local(Compiler *self, const Token *ident);

isize
compiler_new_table(Compiler *self);

void
compiler_adjust_table(Compiler *self, isize i_code, isize n_fields);

void
compiler_set_table(Compiler *self, int i_table, int i_key, int n_pop);

#endif // LULU_COMPILER_H
