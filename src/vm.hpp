#pragma once

#include "private.hpp"
#include "chunk.hpp"
#include "string.hpp"
#include "object.hpp"

using Error = lulu_Error;

static constexpr int MAX_STACK = 256;

struct Error_Handler {
    Error_Handler *prev; // Stack-allocated linked list.
    volatile Error error;
};

struct Call_Frame {
    Slice<Value>       window;
    Function          *function;
    const Instruction *saved_ip;
    int                expected_returned;
};

enum Call_Type {
    CALL_LUA,
    CALL_C,
};

struct lulu_VM {
    Value              stack[MAX_STACK];
    Call_Frame         frames[16];
    int                n_frames;
    Call_Frame        *caller; // Not a reference because it can be reassigned.
    Slice<Value>       window;
    Builder            builder;
    Intern             intern;
    Table              globals;
    lulu_Allocator     allocator;
    void              *allocator_data;
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

/**
 * @note 2025-06-24
 * Assumptions:
 *  1.) Incrementing the VM's view length by 1 is still within bounds of the
 *      main stack.
 */
void
vm_push(lulu_VM &vm, Value v);

void
vm_check_stack(lulu_VM &vm, int n);

[[noreturn]]
void
vm_throw(lulu_VM &vm, Error e);

[[noreturn, gnu::format(printf, 4, 5)]]
void
vm_syntax_error(lulu_VM &vm, String file, int line, const char *fmt, ...);

[[noreturn, gnu::format(printf, 3, 4)]]
void
vm_runtime_error(lulu_VM &vm, const char *act, const char *fmt, ...);

void
vm_concat(lulu_VM &vm, Value &ra, Slice<Value> args);

Call_Type
vm_call(lulu_VM &vm, Value &ra, int argc, int expected_returned);


/**
 * @note 2025-06-16
 *  Assumptions:
 *  1.) The stack was resized properly beforehand, so that doing
 *      pointer arithmetic is still within bounds even if we do not
 *      explicitly check.
 */
Call_Type
vm_return(lulu_VM &vm, Value &ra, int actual_returned);

Error
vm_interpret(lulu_VM &vm, String source, String script);

void
vm_execute(lulu_VM &vm);
