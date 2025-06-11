#pragma once

#include "lulu.h"
#include "private.hpp"
#include "chunk.hpp"
#include "slice.hpp"

using Error = lulu_Error;

static constexpr int MAX_STACK = 256;

struct Error_Handler {
    Error_Handler *prev; // stack-allocated linked list
    volatile Error error;
};

struct lulu_VM {
    Value          stack[MAX_STACK];
    Slice<Value>   window;
    lulu_Allocator allocator;
    void          *allocator_data;
    Chunk         *chunk;
    Error_Handler *error_handler;
};

using Protected_Fn = void (*)(lulu_VM &vm, void *user_ptr);

void
vm_init(lulu_VM &vm, lulu_Allocator allocator, void *allocator_data);

Error
vm_run_protected(lulu_VM &vm, Protected_Fn fn, void *user_ptr);

void
vm_destroy(lulu_VM &vm);

Error
vm_interpret(lulu_VM &vm, Chunk &c);

void
vm_execute(lulu_VM &vm);
