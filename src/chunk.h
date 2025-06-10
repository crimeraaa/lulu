#pragma once

#include "lulu.h"
#include "mem.h"
#include "opcode.h"

typedef lulu_Number Value;

#define DYNAMIC_TYPE Instruction
#include "dynamic.h"

#define DYNAMIC_TYPE Value
#include "dynamic.h"

typedef struct {
    Dynamic(Instruction) code;
    Dynamic(Value)       constants;
} Chunk;

void
chunk_init(Chunk *c);

void
chunk_append(lulu_VM *vm, Chunk *c, Instruction i);

uint32_t
chunk_add_constant(lulu_VM *vm, Chunk *c, Value v);

void
chunk_destroy(lulu_VM *vm, Chunk *c);

void
chunk_dump_all(const Chunk *c);

void
chunk_dump_instruction(const Chunk *c, Instruction ip, int pc);
