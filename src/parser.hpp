#pragma once

#include "lexer.hpp"
#include "chunk.hpp"

// Defined in `parser.cpp`.
struct Block;

constexpr int
PARSER_MAX_RECURSE = 250;


/**
 * @details 2025-07-13
 *  When used as a jump offset, this marks the start of a jump list because
 *  of the following properties.
 *
 *  1.) It is an invalid `pc`, because `pc >= 0`.
 *  2.) It is an infinite loop, because the `ip` at the point the instructions
 *      are dispatched are already incremented. So adding `-1` essentially
 *      undoes the increment, bringing us back to `OP_JUMP`.
 */
#define NO_JUMP     -1

struct LULU_PRIVATE Parser {
    lulu_VM *vm;
    Lexer    lexer;
    Token    consumed;
    Token    lookahead; // Used only in `parser.cpp:constructor()`.
    Builder *builder;
    Block   *block;
    int      last_line; // Line of the token before `consumed`.
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

struct LULU_PRIVATE Expr {
    Expr_Type type;
    isize     patch_true;
    isize     patch_false;
    union {
        Number     number; // Must be first member for brace initialization.
        isize      pc;
        Expr_Table table;
        u32        index;
        u16        reg;
    };

    static constexpr Expr
    make(Expr_Type type)
    {
        Expr e{
            /* type */              type,
            /* patch_true */        NO_JUMP,
            /* patch_false */       NO_JUMP,
            /* <unnamed>::number */ {0},
        };
        return e;
    }

    static constexpr Expr
    make_pc(Expr_Type type, isize pc)
    {
        Expr e = make(type);
        e.pc = pc;
        return e;
    }

    static constexpr Expr
    make_number(Number n)
    {
        Expr e = make(EXPR_NUMBER);
        e.number = n;
        return e;
    }

    static constexpr Expr
    make_reg(Expr_Type type, u16 reg)
    {
        Expr e = make(type);
        e.reg = reg;
        return e;
    }

    static constexpr inline Expr
    make_index(Expr_Type type, u32 index)
    {
        Expr e = make(type);
        e.index = index;
        return e;
    }

    bool
    is_literal() const noexcept
    {
        return EXPR_NIL <= this->type && this->type <= EXPR_CONSTANT;
    }

    // For constant folding purposes, `nil` is also considered a boolean.
    bool
    is_boolean() const noexcept
    {
        return EXPR_NIL <= this->type && this->type <= EXPR_TRUE;
    }

    bool
    is_number() const noexcept
    {
        return this->type == EXPR_NUMBER;
    }

    bool
    is_truthy() const noexcept
    {
        return EXPR_TRUE <= this->type && this->type <= EXPR_CONSTANT;
    }

    bool
    is_falsy() const noexcept
    {
        return EXPR_NIL <= this->type && this->type <= EXPR_FALSE;
    }

    bool
    has_jumps() const noexcept
    {
        return this->patch_true != this->patch_false;
    }

    bool
    has_multret() const noexcept
    {
        return this->type == EXPR_CALL /* || this->type == EXPR_VARARG */;
    }
};

LULU_FUNC Chunk *
parser_program(lulu_VM *vm, OString *source, Stream *z, Builder *b);
