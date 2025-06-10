#pragma once

#include "lulu.hpp"
#include "chunk.hpp"

struct lulu_VM {
    Value          stack[256];
    Chunk         *chunk;
    lulu_Allocator allocator;
    void          *allocator_data;
};

void
vm_execute(lulu_VM &vm, Chunk &c);
