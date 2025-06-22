#pragma once

#include "lulu.h"
#include "private.hpp"
#include "chunk.hpp"
#include "string.hpp"

using Error = lulu_Error;

static constexpr int MAX_STACK = 256;

struct Error_Handler {
    Error_Handler *prev; // Stack-allocated linked list.
    volatile Error error;
};

struct lulu_VM {
    Value              stack[MAX_STACK];
    Slice<Value>       window;
    Builder            builder;
    Intern             intern;
    lulu_Allocator     allocator;
    void              *allocator_data;
    Chunk             *chunk; // Not a reference because it can be reassigned.
    Error_Handler     *error_handler;
    const Instruction *saved_ip; // Used for error handling.
    Object            *objects;  // Linked list of all collectable objects.
};

using Protected_Fn = void (*)(lulu_VM &vm, void *user_ptr);

void
vm_init(lulu_VM &vm, lulu_Allocator allocator, void *allocator_data);

Builder &
vm_get_builder(lulu_VM &vm);

void
vm_destroy(lulu_VM &vm);

Error
vm_run_protected(lulu_VM &vm, Protected_Fn fn, void *user_ptr);

[[noreturn]]
void
vm_throw(lulu_VM &vm, Error e);

[[noreturn, gnu::format(printf, 4, 5)]]
void
vm_syntax_error(lulu_VM &vm, String file, int line, const char *fmt, ...);

[[noreturn, gnu::format(printf, 3, 4)]]
void
vm_runtime_error(lulu_VM &vm, const char *act, const char *fmt, ...);

Error
vm_interpret(lulu_VM &vm, String source, String script);

void
vm_execute(lulu_VM &vm);

void
vm_concat(lulu_VM &vm, Value &ra, Slice<Value> args);
