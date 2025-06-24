#pragma once

#include "lexer.hpp"

// Defined in `compiler.hpp`.
struct Compiler;

struct Parser {
    lulu_VM &vm;
    Lexer    lexer;
    Token    consumed;
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

Parser
parser_make(lulu_VM &vm, String source, String script);

[[noreturn]]
void
parser_error(Parser &p, const char *msg);

Chunk *
parser_program(lulu_VM &vm, String source, String script);
