#pragma once

#include "lulu.h"
#include "mem.h"
#include "opcode.h"
#include "dynamic.h"
#include "value.h"

struct Line_Info {
    int line; // Line number is stored directly in case we skip empty lines.
    int start_pc;
    int end_pc;
};

struct Chunk {
    Dynamic<Value>       constants;
    Dynamic<Instruction> code;
    Dynamic<Line_Info>   line_info;
    int                  stack_used;
};

static constexpr int NO_LINE = -1;

// Disable name mangling
extern "C" {

void
chunk_init(Chunk &c);

void
chunk_append(lulu_VM &vm, Chunk &c, Instruction i, int line);

int
chunk_get_line(const Chunk &c, int pc);

u32
chunk_add_constant(lulu_VM &vm, Chunk &c, Value v);

void
chunk_destroy(lulu_VM &vm, Chunk &c);

}; // extern "C"
