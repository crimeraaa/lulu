#pragma once

#include "lulu.h"
#include "opcode.hpp"
#include "dynamic.hpp"
#include "value.hpp"
#include "string.hpp"

struct Line_Info {
    int line; // Line number is stored directly in case we skip empty lines.
    int start_pc;
    int end_pc;
};

struct Chunk {
    Dynamic<Value>       constants;
    Dynamic<Instruction> code;
    Dynamic<Line_Info>   line_info;
    String               source;
    int                  stack_used;
};

static constexpr int NO_LINE = -1;

void
chunk_init(Chunk &c, String source);

void
chunk_append(lulu_VM &vm, Chunk &c, Instruction i, int line);

void
chunk_append(lulu_VM &vm, Chunk &c, OpCode op, u8 a, u16 b, u16 c2, int line);

void
chunk_append(lulu_VM &vm, Chunk &c, OpCode op, u8 a, u32 bx, int line);

int
chunk_get_line(const Chunk &c, int pc);

u32
chunk_add_constant(lulu_VM &vm, Chunk &c, Value v);

void
chunk_destroy(lulu_VM &vm, Chunk &c);
