#pragma once

#include "lulu.h"
#include "mem.h"
#include "opcode.h"
#include "dynamic.h"

typedef lulu_Number Value;

typedef struct {
    Dynamic<Instruction> code;
    Dynamic<Value>       constants;
    int                  stack_used;
} Chunk;

void
chunk_init(Chunk &c);

void
chunk_append(lulu_VM &vm, Chunk &c, Instruction i);

u32
chunk_add_constant(lulu_VM &vm, Chunk &c, Value v);

void
chunk_destroy(lulu_VM &vm, Chunk &c);

void
chunk_dump_all(const Chunk &c);

void
chunk_dump_instruction(const Chunk &c, Instruction ip, int pc);
