#pragma once

#include "chunk.hpp"
#include "lexer.hpp"

constexpr int PARSER_MAX_RECURSE = 250;


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
#define NO_JUMP -1

struct Parser {
    lulu_VM *vm;
    Lexer    lexer;
    Token    current;
    Token    lookahead; // Used only in `parser.cpp:constructor()`.
    Builder *builder;
    int      last_line; // Line of the token consumed, NOT `current`.
    int      n_calls;   // How many recursive C calls are we currently in?
};

enum Precedence : i8 {
    PREC_NONE = -1,  // Not a valid lookup table key.
    PREC_OR,         // or
    PREC_AND,        // and
    PREC_EQUALITY,   // == ~=
    PREC_COMPARISON, // < <= > >=
    PREC_CONCAT,     // ..
    PREC_TERMINAL,   // + -
    PREC_FACTOR,     // * / %
    PREC_EXPONENT,   // ^
    PREC_UNARY,      // - not #
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
    EXPR_INDEXED,    // Table register in `table.reg` and key RK in
                     // `table.field_rk`.
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

    // pc of truthy patch lists, mainly for logical or.
    int patch_true;

    // pc of falsy patch lists, mainly for if-statements and logical-and.
    int patch_false;
    union {
        Number     number; // Must be first member for brace initialization.
        Expr_Table table;
        int        pc;
        u32        index;
        u16        reg;
    };

    static constexpr Expr
    make(Expr_Type type)
    {
        Expr e{
            /* type */ type,
            /* patch_true */ NO_JUMP,
            /* patch_false */ NO_JUMP,
            /* <unnamed>::number */ {0},
        };
        return e;
    }

    static constexpr Expr
    make_pc(Expr_Type type, isize pc)
    {
        Expr e = make(type);
        e.pc   = pc;
        return e;
    }

    static constexpr Expr
    make_number(Number n)
    {
        Expr e   = make(EXPR_NUMBER);
        e.number = n;
        return e;
    }

    static constexpr Expr
    make_reg(Expr_Type type, u16 reg)
    {
        Expr e = make(type);
        e.reg  = reg;
        return e;
    }

    static constexpr inline Expr
    make_index(Expr_Type type, u32 index)
    {
        Expr e  = make(type);
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

Chunk *
parser_program(lulu_VM *vm, OString *source, Stream *z, Builder *b);
