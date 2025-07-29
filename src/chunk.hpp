#pragma once

#include "opcode.hpp"
#include "dynamic.hpp"
#include "value.hpp"
#include "string.hpp"

struct LULU_PRIVATE Line_Info {
    int   line; // Line number is stored directly in case we skip empty lines.
    isize start_pc;
    isize end_pc;
};

struct LULU_PRIVATE Local {
    OString *ident;
    isize    start_pc;
    isize    end_pc;
};

struct LULU_PRIVATE Chunk {
    OBJECT_HEADER;
    Dynamic<Value>       constants;
    Dynamic<Instruction> code;
    Dynamic<Line_Info>   lines;
    Dynamic<Local>       locals; // Information of ALL locals in the function.

    // Debug/VM information
    OString *source;
    int line_defined;
    int last_line_defined;
    int stack_used;
};

static constexpr u16 VARARG = Instruction::MAX_B;
static constexpr int NO_LINE = -1;

LULU_FUNC Chunk *
chunk_new(lulu_VM *vm, OString *source);

LULU_FUNC isize
chunk_append(lulu_VM *vm, Chunk *p, Instruction i, int line);

LULU_FUNC int
chunk_get_line(const Chunk *p, isize pc);


/**
 * @param v
 *      The value we wish to push to the `c->constants` dynamic array.
 *
 * @return
 *      The index of `v` in the constants array.
 */
LULU_FUNC u32
chunk_add_constant(lulu_VM *vm, Chunk *p, const Value &v);

LULU_FUNC isize
chunk_add_local(lulu_VM *vm, Chunk *p, OString *ident);


/**
 * @param local_number
 *      The 1-based index of the local we want to get.
 *
 * @param pc
 *      The index of the instruction where `local_number` is valid.
 */
LULU_FUNC const char *
chunk_get_local(const Chunk *p, int local_number, isize pc);
