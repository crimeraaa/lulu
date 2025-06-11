#pragma once

#include "lulu.h"
#include "mem.h"
#include "opcode.h"
#include "dynamic.h"

using Number = lulu_Number;
using Value  = lulu_Number;

struct Chunk {
    Dynamic<Value>       constants;
    Dynamic<Instruction> code;
    Dynamic<int>         lines;
    int                  stack_used;
};

void
chunk_init(Chunk &c);

void
chunk_append(lulu_VM &vm, Chunk &c, Instruction i, int line);

u32
chunk_add_constant(lulu_VM &vm, Chunk &c, Value v);

void
chunk_destroy(lulu_VM &vm, Chunk &c);
