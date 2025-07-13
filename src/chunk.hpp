#pragma once

#include "opcode.hpp"
#include "dynamic.hpp"
#include "value.hpp"
#include "string.hpp"

struct Line_Info {
    int   line; // Line number is stored directly in case we skip empty lines.
    isize start_pc;
    isize end_pc;
};

struct Local {
    OString *identifier;
    isize    start_pc;
    isize    end_pc;
};

struct Chunk {
    OBJECT_HEADER;
    Dynamic<Value>       constants;
    Dynamic<Instruction> code;
    Dynamic<Line_Info>   lines;
    Dynamic<Local>       locals;    // Information of ALL locals in the function.
    Table               *indexes;   // Maps values to indexes in `constants`.
    LString              source;
    int                  stack_used;
};

static constexpr u16 VARARG = OPCODE_MAX_B;
static constexpr int NO_LINE = -1;

LULU_FUNC Chunk *
chunk_new(lulu_VM *vm, LString source, Table *indexes);

LULU_FUNC isize
chunk_append(lulu_VM *vm, Chunk *c, Instruction i, int line);

LULU_FUNC int
chunk_get_line(const Chunk *c, isize pc);

LULU_FUNC u32
chunk_add_constant(lulu_VM *vm, Chunk *c, Value v);

LULU_FUNC isize
chunk_add_local(lulu_VM *vm, Chunk *c, OString *id);


/**
 * @param local_number
 *      The 1-based index of the local we want to get.
 *
 * @param pc
 *      The index of the instruction where `local_number` is valid.
 */
LULU_FUNC const char *
chunk_get_local(const Chunk *c, int local_number, isize pc);
