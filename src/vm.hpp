#pragma once

#include "small_array.hpp"
#include "private.hpp"
#include "object.hpp"
#include "stream.hpp"

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
    int                to_return;

    bool
    is_c() const noexcept
    {
        return this->function->is_c();
    }

    bool
    is_lua() const noexcept
    {
        return this->function->is_lua();
    }

    Closure_C *
    to_c()
    {
        return this->function->to_c();
    }

    Closure_Lua *
    to_lua()
    {
        return this->function->to_lua();
    }
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
    lulu_CFunction     panic_fn;
    lulu_Allocator     allocator;
    void              *allocator_data;
    Error_Handler     *error_handler;
    const Instruction *saved_ip; // Used for error handling.
    Object            *objects;  // Linked list of all collectable objects.

    LULU_PRIVATE
    lulu_VM() = default;
};

using Protected_Fn = void (*)(lulu_VM *vm, void *user_ptr);

LULU_FUNC Builder *
vm_get_builder(lulu_VM *vm);

LULU_FUNC Value *
vm_base_ptr(lulu_VM *vm);

LULU_FUNC Value *
vm_top_ptr(lulu_VM *vm);


/**
 * @brief
 *      Gets the absolute index of `v` in `vm->stack`.
 */
LULU_FUNC isize
vm_absindex(lulu_VM *vm, const Value *v);

LULU_FUNC isize
vm_base_absindex(lulu_VM *vm);

LULU_FUNC isize
vm_top_absindex(lulu_VM *vm);


/**
 * @brief
 *      Wraps a call to `fn(vm, user_ptr)` with a try-catch block. In case
 *      of errors, the error message, as a string, is left on the top of
 *      the stack.
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


/**
 * @return
 *      `true` if `v` is already a number or a string convertible to a number,
 *      or else `false. If `true` then `*out` is assigned.
 *
 * @note(2025-07-28)
 *      `v` and `out` may alias.
 */
LULU_FUNC bool
vm_to_number(const Value *v, Value *out);


/**
 * @note(2025-07-21) Assumptions
 *
 *  1.) This function only ever deals with conversion of non-`string`
 *      (i.e. `number`) to `string`.
 */
LULU_FUNC bool
vm_to_string(lulu_VM *vm, Value *in_out);

LULU_FUNC const char *
vm_push_string(lulu_VM *vm, LString s);

[[gnu::format(printf, 2, 3)]]
LULU_FUNC const char *
vm_push_fstring(lulu_VM *vm, const char *fmt, ...);

LULU_FUNC const char *
vm_push_vfstring(lulu_VM *vm, const char *fmt, va_list args);

[[noreturn, gnu::format(printf, 2, 3)]]
LULU_FUNC void
vm_runtime_error(lulu_VM *vm, const char *fmt, ...);

LULU_FUNC void
vm_concat(lulu_VM *vm, Value *ra, Slice<Value> args);

LULU_FUNC void
vm_call(lulu_VM *vm, const Value *ra, int n_args, int n_rets);


/**
 * @brief
 *      Prepares a function call for the Lua or C function at `ra`.
 *      If it is a C function, it is called directly. Otherwise,
 *      if it is a Lua function, it can be called by `vm_execute()`.
 */
LULU_FUNC Call_Type
vm_call_init(lulu_VM *vm, const Value *ra, int argc, int to_return);


/**
 * @note(2025-06-16) Assumptions:
 *
 *  1.) The stack was resized properly beforehand, so that doing
 *      pointer arithmetic is still within bounds even if we do not
 *      explicitly check.
 *
 * @note(2025-08-06) Assumptions:
 *
 *  2.) C calls are completed within `vm_call_init()`. This will
 *      simply pop the temporary call frame we used then..
 *
 *  3.) Otherwise, Lua calls return to `vm_execute()`.
 */
LULU_FUNC void
vm_call_fini(lulu_VM *vm, const Slice<Value> &results);

LULU_FUNC Error
vm_load(lulu_VM *vm, LString source, Stream *z);

LULU_FUNC bool
vm_table_get(lulu_VM *vm, const Value *t, Value k, Value *out);

LULU_FUNC void
vm_table_set(lulu_VM *vm, const Value *t, const Value *k, Value v);

LULU_FUNC void
vm_execute(lulu_VM *vm, int n_calls);
