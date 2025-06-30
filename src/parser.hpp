#pragma once

#include "lexer.hpp"
#include "chunk.hpp"

struct Parser {
    lulu_VM *vm;
    Lexer    lexer;
    Token    consumed;
    Builder *builder;
};

enum Precedence : u8 {
    PREC_NONE,
    PREC_AND,
    PREC_OR,
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
    EXPR_TRUE,       // Literal `true`.
    EXPR_FALSE,      // Literal `false`.
    EXPR_NUMBER,     // Number literal we haven't yet stored. Use `number`.
    EXPR_CONSTANT,   // Literal value stored in constants array. Use `index`.
    EXPR_GLOBAL,     // Global variable named stored in `index`.
    EXPR_CALL,       // `OP_CALL`. Use `pc`.
    EXPR_RELOCABLE,  // Instruction without register A finalized- use `pc`.
    EXPR_DISCHARGED, // Instruction with a finalized register.
};

struct Expr {
    Expr_Type type;
    int       line;
    union {
        Number number; // Must be first member for brace initialization.
        u32    index;
        int    pc;
        u8     reg;
    };
};

LULU_FUNC Parser
parser_make(lulu_VM *vm, LString source, LString script, Builder *b);

[[noreturn]]
LULU_FUNC void
parser_error(Parser *p, const char *msg);

LULU_FUNC Chunk *
parser_program(lulu_VM *vm, LString source, LString script, Builder *b);
