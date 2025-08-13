#pragma once

#include "opcode.hpp"
#include "dynamic.hpp"
#include "value.hpp"
#include "string.hpp"

struct Line_Info {
    int line; // Line number is stored directly in case we skip empty lines.
    int start_pc;
    int end_pc;
};

struct Local {
    OString *ident;
    int start_pc;
    int end_pc;
};

struct Chunk {
    OBJECT_HEADER;
    Dynamic<Value> constants;
    Dynamic<Instruction> code;
    Dynamic<Line_Info> lines;

    // Information of all possible locals, in order, for the function.
    Dynamic<Local> locals;

    // Debug/VM information
    OString *source;
    int line_defined;
    int last_line_defined;
    u16 n_params;
    u16 stack_used;
};

static constexpr u16 VARARG = Instruction::MAX_B;
static constexpr int NO_LINE = -1;

Chunk *
chunk_new(lulu_VM *vm, OString *source);

void
chunk_delete(lulu_VM *vm, Chunk *p);

int
chunk_append(lulu_VM *vm, Chunk *p, Instruction i, int line);

int
chunk_get_line(const Chunk *p, int pc);


/**
 * @param v
 *      The value we wish to push to the `c->constants` dynamic array.
 *
 * @return
 *      The index of `v` in the constants array.
 */
u32
chunk_add_constant(lulu_VM *vm, Chunk *p, Value v);

int
chunk_add_local(lulu_VM *vm, Chunk *p, OString *ident);


/**
 * @param local_number
 *      The 1-based index of the local we want to get.
 *
 * @param pc
 *      The index of the instruction where `local_number` is valid.
 */
const char *
chunk_get_local(const Chunk *p, int local_number, int pc);
