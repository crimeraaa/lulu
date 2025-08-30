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

// Somewhat optimized for size, *could* shave off some more by changing
// slice lengths to `int` and packing appropriately, but do we really care?
struct Chunk : Object_Header {
    // Only used during the mark and traverse phases of GC.
    // This object is independent only during compilation, where it resides
    // on the stack. Afterwards it only ever exists as the main chunk of
    // a particular closure or as a child for local functions.
    GC_List *gc_list;

    // Information of all possible locals, in order, for the function.
    // Finding a local is thus possible if you have the program counter
    // it is active at.
    Slice<Local> locals;

    // List of all upvalue names, in order.
    Slice<OString *> upvalues;

    // List of all constant values used by the function, in order.
    Slice<Value> constants;

    // Chunks needed for all closures defined within this function.
    Slice<Chunk *> children;

    // Raw bytecode. While compiling, `len()` refers to the allocated capacity.
    // The actual length is held by the parent Compiler. When done compiling,
    // it is shrunk to fit.
    Slice<Instruction> code;

    // Maps bytecode indices to source code lines.
    Slice<Line_Info> lines;

    // Debug/VM information
    OString *source;
    int line_defined;
    int last_line_defined;
    u8 n_upvalues;
    u8 n_params;
    u8 stack_used;
};

static constexpr u16 VARARG  = Instruction::MAX_B;
static constexpr int NO_LINE = -1;

Chunk *
chunk_new(lulu_VM *L, OString *source);

void
chunk_delete(lulu_VM *L, Chunk *p);

template<class T, class N>
inline N
chunk_push(lulu_VM *L, Slice<T> *s, T v, N *n)
{
    isize i = static_cast<isize>((*n)++);
    if (i + 1 > len(*s)) {
        slice_resize(L, s, mem_next_pow2(max(i + 1, 8_i)));
    }
    (*s)[i] = v;
    return static_cast<N>(i);
}

inline int
chunk_code_push(lulu_VM *L, Chunk *p, Instruction i, int *pc)
{
    return chunk_push(L, &p->code, i, pc);
}

void
chunk_line_push(lulu_VM *L, Chunk *p, int pc, int line, int *n);

int
chunk_line_get(const Chunk *p, int pc);

inline u32
chunk_constant_push(lulu_VM *L, Chunk *p, Value v, u32 *n)
{
    return chunk_push(L, &p->constants, v, n);
}

inline int
chunk_local_push(lulu_VM *L, Chunk *p, OString *ident, int *n)
{
    Local local{ident, 0, 0};
    return chunk_push(L, &p->locals, local, n);
}

inline int
chunk_child_push(lulu_VM *L, Chunk *p, Chunk *child, int *n)
{
    return chunk_push(L, &p->children, child, n);
}

inline int
chunk_upvalue_push(lulu_VM *L, Chunk *p, OString *ident, u8 *n)
{
    return chunk_push(L, &p->upvalues, ident, n);
}


/**
 * @param local_number
 *      The 1-based index of the local we want to get.
 *
 * @param pc
 *      The index of the instruction where `local_number` is valid.
 */
const char *
chunk_get_local(const Chunk *p, int local_number, int pc);
