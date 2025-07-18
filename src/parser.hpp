#pragma once

#include "lexer.hpp"
#include "chunk.hpp"

// Defined in `parser.cpp`.
struct Block;

constexpr int
PARSER_MAX_RECURSE = 250;

struct Parser {
    lulu_VM *vm;
    Lexer    lexer;
    Token    consumed;
    Token    lookahead; // Used only in `parser.cpp:constructor()`.
    Builder *builder;
    Block   *block;
    int      n_calls;   // How many recursive C calls are we currently in?
};

enum Precedence : u8 {
    PREC_NONE,
    PREC_OR,
    PREC_AND,
    PREC_EQUALITY,  // == ~=
    PREC_COMPARISON, // < <= > >=
    PREC_CONCAT,    // ..
    PREC_TERMINAL,  // + -
    PREC_FACTOR,    // * / %
    PREC_EXPONENT,  // ^
    PREC_UNARY,     // - not #
};

enum Binary_Type {
    BINARY_NONE,                        // PREC_NONE
    BINARY_AND, BINARY_OR,              // PREC_AND, PREC_OR
    BINARY_ADD, BINARY_SUB,             // PREC_TERMINAL
    BINARY_MUL, BINARY_DIV, BINARY_MOD, // PREC_FACTOR
    BINARY_POW,                         // PREC_EXPONENT
    BINARY_EQ,  BINARY_LT, BINARY_LEQ,  // PREC_COMPARISON, cond=true
    BINARY_NEQ, BINARY_GT, BINARY_GEQ,  // PREC_COMPARISON, cond=false
    BINARY_CONCAT,                      // PREC_CONCAT
};

enum Expr_Type {
    EXPR_NONE,
    EXPR_NIL,        // Literal `nil`.
    EXPR_FALSE,      // Literal `false`.
    EXPR_TRUE,       // Literal `true`.
    EXPR_NUMBER,     // Number literal we haven't yet stored. Use `number`.
    EXPR_CONSTANT,   // Literal value stored in constants array. Use `index`.
    EXPR_GLOBAL,     // Global variable named stored in `index`.
    EXPR_LOCAL,      // Local variable register stored in `reg`.
    EXPR_INDEXED,    // Table register in `table.reg` and key RK in `table.field_rk`.
    EXPR_JUMP,       // An `OP_JUMP` chain. Use `pc`.
    EXPR_CALL,       // `OP_CALL`. Use `pc`.
    EXPR_RELOCABLE,  // Instruction without register A finalized- use `pc`.
    EXPR_DISCHARGED, // Instruction with a finalized register.
};

struct Expr_Table {
    u16 reg;
    u16 field_rk;
};

struct Expr {
    Expr_Type type;
    int       line;
    isize     patch_true;
    isize     patch_false;
    union {
        Number     number; // Must be first member for brace initialization.
        isize      pc;
        Expr_Table table;
        u32        index;
        u16        reg;
    };
};

LULU_FUNC inline bool
expr_is_literal(const Expr *e)
{
    return EXPR_NIL <= e->type && e->type <= EXPR_CONSTANT;
}

LULU_FUNC inline bool
expr_is_boolean(const Expr *e)
{
    return EXPR_NIL <= e->type && e->type <= EXPR_TRUE;
}

LULU_FUNC inline bool
expr_is_number(const Expr *e)
{
    return e->type == EXPR_NUMBER;
}

LULU_FUNC inline bool
expr_is_truthy(const Expr *e)
{
    return EXPR_TRUE <= e->type && e->type <= EXPR_CONSTANT;
}

LULU_FUNC inline bool
expr_is_falsy(const Expr *e)
{
    return EXPR_NIL <= e->type && e->type <= EXPR_FALSE;
}

LULU_FUNC inline bool
expr_has_jumps(const Expr *e)
{
    return e->patch_true != e->patch_false;
}

LULU_FUNC Parser
parser_make(lulu_VM *vm, LString source, LString script, Builder *b);

[[noreturn]]
LULU_FUNC void
parser_error(Parser *p, const char *msg);

[[noreturn]]
LULU_FUNC void
parser_error_at(Parser *p, const Token *t, const char *msg);

LULU_FUNC Chunk *
parser_program(lulu_VM *vm, LString source, LString script, Builder *b);
