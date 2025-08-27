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

struct Chunk {
    OBJECT_HEADER;
    // Information of all possible locals, in order, for the function.
    Dynamic<Local> locals;

    // List of all upvalue names, in order.
    Dynamic<OString *> upvalues;

    // List of all constant values used by the function, in order.
    Dynamic<Value> constants;

    // Chunks needed for all closures defined within this function.
    Dynamic<Chunk *> children;

    // Raw bytecode.
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
chunk_new(lulu_VM *vm, OString *source);

void
chunk_delete(lulu_VM *vm, Chunk *p);

template<class T>
inline int
chunk_slice_push(lulu_VM *vm, Slice<T> *s, T v, int *n)
{
    int i = (*n)++;
    if (i + 1 > len(*s)) {
        slice_resize(vm, s, mem_next_pow2(max(i + 1, 8)));
    }
    (*s)[i] = v;
    return i;
}

int
chunk_add_code(lulu_VM *vm, Chunk *p, Instruction i, int line, int *n);

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
