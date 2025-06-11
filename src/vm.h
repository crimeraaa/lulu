#pragma once

#include "lulu.h"
#include "private.h"
#include "chunk.h"

#define SLICE_TYPE Value
#include "slice.h"

struct lulu_VM {
    Value          stack[16];
    lulu_Allocator allocator;
    void *         allocator_data;
};

void
vm_init(lulu_VM &vm, lulu_Allocator allocator, void *allocator_data);

void
vm_execute(lulu_VM &vm, Chunk &c);
