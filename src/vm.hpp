#pragma once

#include "small_array.hpp"
#include "private.hpp"
#include "object.hpp"

using Error = lulu_Error;

static constexpr int MAX_STACK = 256;

struct LULU_PRIVATE Error_Handler {
    Error_Handler *prev; // Stack-allocated linked list.
    volatile Error error;
};

struct LULU_PRIVATE Call_Frame {
    Slice<Value>       window;
    Closure           *function;
    const Instruction *saved_ip;
    int                expected_returned;
};

enum Call_Type {
    CALL_LUA,
    CALL_C,
};

using Stack_Array = Array<Value, MAX_STACK>;
using Frame_Array = Small_Array<Call_Frame, 16>;

struct LULU_PUBLIC lulu_VM {
    Stack_Array        stack;
    Frame_Array        frames;
    Call_Frame        *caller; // Not a reference because it can be reassigned.
    Slice<Value>       window;
    Value              globals;
    Builder            builder;
    Intern             intern;
    lulu_Allocator     allocator;
    void              *allocator_data;
    Error_Handler     *error_handler;
    const Instruction *saved_ip; // Used for error handling.
    Object            *objects;  // Linked list of all collectable objects.
};

using Protected_Fn = void (*)(lulu_VM *vm, void *user_ptr);

LULU_FUNC bool
vm_init(lulu_VM *vm, lulu_Allocator allocator, void *allocator_data);

LULU_FUNC Builder *
vm_get_builder(lulu_VM *vm);

LULU_FUNC void
vm_destroy(lulu_VM *vm);

LULU_FUNC Value *
vm_base_ptr(lulu_VM *vm);

LULU_FUNC Value *
vm_top_ptr(lulu_VM *vm);


/**
 * @brief
 *  -   Gets the absolute index of `v` in `vm->stack`.
 */
LULU_FUNC isize
vm_absindex(lulu_VM *vm, Value *v);

LULU_FUNC isize
vm_base_absindex(lulu_VM *vm);

LULU_FUNC isize
vm_top_absindex(lulu_VM *vm);

/**
 * @brief
 *  -   Wraps a call to `fn(vm, user_ptr)` with a try-catch block.
 *  -   In case of errors, the error message, as a string, is left on the
 *      top of the stack.
 */
LULU_FUNC Error
vm_run_protected(lulu_VM *vm, Protected_Fn fn, void *user_ptr);


/**
 * @note 2025-06-24
 * Assumptions:
 *  1.) Incrementing the VM's view length by 1 is still within bounds of the
 *      main stack.
 */
LULU_FUNC void
vm_push(lulu_VM *vm, Value v);

LULU_FUNC Value
vm_pop(lulu_VM *vm);

LULU_FUNC void
vm_check_stack(lulu_VM *vm, int n);

[[noreturn]]
LULU_FUNC void
vm_throw(lulu_VM *vm, Error e);

LULU_FUNC const char *
vm_push_string(lulu_VM *vm, LString s);

LULU_FUNC const char *
vm_push_fstring(lulu_VM *vm, const char *fmt, ...);

LULU_FUNC const char *
vm_push_vfstring(lulu_VM *vm, const char *fmt, va_list args);

[[noreturn, gnu::format(printf, 4, 5)]]
LULU_FUNC void
vm_syntax_error(lulu_VM *vm, LString source, int line, const char *fmt, ...);

[[noreturn, gnu::format(printf, 3, 4)]]
LULU_FUNC void
vm_runtime_error(lulu_VM *vm, const char *act, const char *fmt, ...);

LULU_FUNC void
vm_concat(lulu_VM *vm, Value *ra, Slice<Value> args);

LULU_FUNC void
vm_call(lulu_VM *vm, Value *ra, int n_args, int n_rets);


/**
 * @brief
 *  -   Prepares a function call for the Lua or C function at `ra`.
 *  -   If it is a C function, it is called directly.
 *  -   Otherwise, if it is a Lua function, it can be called by `vm_execute()`.
 */
LULU_FUNC Call_Type
vm_call_init(lulu_VM *vm, Value *ra, int argc, int expected_returned);


/**
 * @note(2025-06-16) Assumptions
 *
 *  1.) The stack was resized properly beforehand, so that doing
 *      pointer arithmetic is still within bounds even if we do not
 *      explicitly check.
 */
LULU_FUNC Call_Type
vm_call_fini(lulu_VM *vm, Value *ra, int actual_returned);

LULU_FUNC Error
vm_load(lulu_VM *vm, LString source, LString script);

LULU_FUNC bool
vm_table_get(lulu_VM *vm, const Value *t, Value k, Value *out);

LULU_FUNC void
vm_table_set(lulu_VM *vm, const Value *t, const Value *k, Value v);

LULU_FUNC void
vm_execute(lulu_VM *vm, int n_calls);
