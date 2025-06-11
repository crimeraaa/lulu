#pragma once

#include "lulu.h"
#include "private.h"
#include "chunk.h"
#include "slice.h"

static constexpr int MAX_STACK = 256;

struct lulu_VM {
    Value          stack[MAX_STACK];
    lulu_Allocator allocator;
    void *         allocator_data;
};

void
vm_init(lulu_VM &vm, lulu_Allocator allocator, void *allocator_data);

void
vm_execute(lulu_VM &vm, Chunk &c);
