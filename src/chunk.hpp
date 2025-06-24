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

struct Chunk {
    OBJECT_HEADER;
    Dynamic<Value>       constants;
    Dynamic<Instruction> code;
    Dynamic<Line_Info>   line_info;
    Table               *indexes;   // Maps values to indexes in `constants`.
    String               source;
    int                  stack_used;
};

static constexpr int NO_LINE = -1;

Chunk *
chunk_new(lulu_VM &vm, String source, Table *indexes);

int
chunk_append(lulu_VM &vm, Chunk &c, Instruction i, int line);

int
chunk_get_line(const Chunk &c, int pc);

u32
chunk_add_constant(lulu_VM &vm, Chunk &c, Value v);
