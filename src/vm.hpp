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
    Closure           *function;
    const Instruction *saved_ip;
    int                expected_returned;
};

enum Call_Type {
    CALL_LUA,
    CALL_C,
};

struct lulu_VM {
    Array<Value, MAX_STACK>     stack;
    Small_Array<Call_Frame, 16> frames;

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

using Protected_Fn = void (*)(lulu_VM *vm, void *user_ptr);

void
vm_init(lulu_VM *vm, lulu_Allocator allocator, void *allocator_data);

Builder &
vm_get_builder(lulu_VM *vm);

void
vm_destroy(lulu_VM *vm);


/**
 * @brief
 *  -   Wraps a call to `fn(vm, user_ptr)` with a try-catch block.
 *  -   In case of errors, the error message, as a string, is left on the
 *      top of the stack.
 */
Error
vm_run_protected(lulu_VM *vm, Protected_Fn fn, void *user_ptr);


/**
 * @note 2025-06-24
 * Assumptions:
 *  1.) Incrementing the VM's view length by 1 is still within bounds of the
 *      main stack.
 */
void
vm_push(lulu_VM *vm, Value v);

Value
vm_pop(lulu_VM *vm);

void
vm_check_stack(lulu_VM *vm, int n);

[[noreturn]]
void
vm_throw(lulu_VM *vm, Error e);

const char *
vm_push_string(lulu_VM *vm, String s);

const char *
vm_push_fstring(lulu_VM *vm, const char *fmt, ...);

const char *
vm_push_vfstring(lulu_VM *vm, const char *fmt, va_list args);

[[noreturn, gnu::format(printf, 4, 5)]]
void
vm_syntax_error(lulu_VM *vm, String source, int line, const char *fmt, ...);

[[noreturn, gnu::format(printf, 3, 4)]]
void
vm_runtime_error(lulu_VM *vm, const char *act, const char *fmt, ...);

void
vm_concat(lulu_VM *vm, Value &ra, Slice<Value> args);

void
vm_call(lulu_VM *vm, Value &ra, int n_args, int n_rets);


/**
 * @brief
 *  -   Prepares a function call for the Lua or C function at `ra`.
 *  -   If it is a C function, it is called directly.
 *  -   Otherwise, if it is a Lua function, it can be called by `vm_execute()`.
 */
Call_Type
vm_call_init(lulu_VM *vm, Value &ra, int argc, int expected_returned);


/**
 * @note 2025-06-16
 *  Assumptions:
 *  1.) The stack was resized properly beforehand, so that doing
 *      pointer arithmetic is still within bounds even if we do not
 *      explicitly check.
 */
Call_Type
vm_call_fini(lulu_VM *vm, Value &ra, int actual_returned);

Error
vm_load(lulu_VM *vm, String source, String script);

void
vm_execute(lulu_VM *vm);
