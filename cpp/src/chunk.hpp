#pragma once

#include "dynamic.hpp"
#include "opcode.hpp"
#include "string.hpp"
#include "value.hpp"

struct Line_Info {
    int line; // Line number is stored directly in case we skip empty lines.
    int start_pc;
    int end_pc;
};

struct Local {
    OString *ident;
    int      start_pc;
    int      end_pc;
};

struct Chunk : Object_Header {
    // Information of all possible locals, in order, for the function.
    Dynamic<Local> locals;

    // List of all upvalue names, in order.
    Dynamic<OString *> upvalues;

    // List of all constant values used by the function, in order.
    Dynamic<Value> constants;

    // Chunks needed for all closures defined within this function.
    Dynamic<Chunk *> children;

    // Raw bytecode. While compiling, `len()` refers to the allocated capacity.
    // The actual length is held by the parent Compiler. When done compiling,
    // it is shrunk to fit.
    Slice<Instruction> code;

    // Maps bytecode indices to source code lines.
    Dynamic<Line_Info> lines;

    // Debug/VM information
    OString *source;
    int      line_defined;
    int      last_line_defined;
    u8       n_upvalues;
    u8       n_params;
    u8       stack_used;
};

static constexpr u16 VARARG  = Instruction::MAX_B;
static constexpr int NO_LINE = -1;

Chunk *
chunk_new(lulu_VM *L, OString *source);

void
chunk_delete(lulu_VM *L, Chunk *p);

int
chunk_add_code(lulu_VM *L, Chunk *p, Instruction i, int line, int *n);

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
chunk_add_constant(lulu_VM *L, Chunk *p, Value v);

int
chunk_add_local(lulu_VM *L, Chunk *p, OString *ident);


/**
 * @param local_number
 *      The 1-based index of the local we want to get.
 *
 * @param pc
 *      The index of the instruction where `local_number` is valid.
 */
const char *
chunk_get_local(const Chunk *p, int local_number, int pc);
