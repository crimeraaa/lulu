#pragma once

#include <stdlib.h> // exit

#include "object.hpp"
#include "private.hpp"
#include "stream.hpp"
#include "gc.hpp"

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

struct lulu_Global {
    lulu_CFunction panic_fn;
    lulu_Allocator allocator;

    // User-data pointer passed to `allocator`.
    void *allocator_data;

    // Buffer used for string concatentation.
    Builder builder;

    // Hash table of all interned strings.
    Intern intern;

    // How much memory are we currently *managing*?
    usize n_bytes_allocated;

    // When `n_bytes_allocated` exceeds this, run the GC.
    usize gc_threshold;

    // Used only when calling `lulu_gc(L, LULU_GC_RESTART)`.
    usize gc_prev_threshold;

    // Linked list of all collectable objects.
    Object_List *objects;

    // Filled up during the mark phase of GC and traversed during trace phase.
    // Never modified after mark phase.
    GC_List *gray_head;

    // The very last node in 'gray_list'. This is useful when appending
    // child nodes from roots so that we do not mess up the iteration.
    //
    // Never used during mark phase. Filled up during trace phase.
    // Can be modified in-place during trace phase.
    GC_List *gray_tail;

    GC_State gc_state;
};

struct LULU_PUBLIC lulu_VM {
    lulu_Global       *G;
    Stack_Array        stack;
    Frame_Array        frames;
    Call_Frame        *caller; // Not a reference because it can be reassigned.
    Slice<Value>       window;
    Value              globals;
    Error_Handler     *error_handler;
    const Instruction *saved_ip; // Used for error handling.

    // Linked list of open upvalues across all active stack frames.
    // Helps with variable reuse.
    Object_List *open_upvalues;

    LULU_PRIVATE
    lulu_VM() = default;
};


inline lulu_Global *
G(lulu_VM *L)
{
    return L->G;
}

using Protected_Fn = void (*)(lulu_VM *L, void *user_ptr);

Builder *
vm_get_builder(lulu_VM *L);

Value *
vm_base_ptr(lulu_VM *L);

Value *
vm_top_ptr(lulu_VM *L);


/** @brief Gets the absolute index of `v` the VM stack. */
int
vm_absindex(lulu_VM *L, const Value *v);

int
vm_base_absindex(lulu_VM *L);

int
vm_top_absindex(lulu_VM *L);


/** @brief Wraps a call to `fn(L, user_ptr)` with a try-catch block.
 *
 * @details
 *  In case of errors, the stack frame before the call is restored and the error
 *  message, a string, pushed to the stack.
 */
Error
vm_pcall(lulu_VM *L, Protected_Fn fn, void *user_ptr);


/** @brief Wraps a call to `fn(L, user_ptr)` with a try-catch block. */
Error
vm_run_protected(lulu_VM *L, Protected_Fn fn, void *user_ptr);


/**
 * @note 2025-06-24
 * Assumptions:
 *  1.) Incrementing the VM's view length by 1 is still within bounds of the
 *      main stack.
 */
inline void
vm_push_value(lulu_VM *L, Value v)
{
    isize i = L->window.len++;
    L->window[i] = v;
}

inline void
vm_pop_value(lulu_VM *L)
{
    // Do not decrement too much.
    lulu_assert(L->window.len > 0);
    L->window.len -= 1;
}

void
vm_check_stack(lulu_VM *L, int n);

[[noreturn]] void
vm_throw(lulu_VM *L, Error e);


/**
 * @return
 *      `true` if `v` is already a number or a string convertible to a number,
 *      or else `false. If `true` then `*out` is assigned.
 *
 * @note(2025-07-28)
 *      `v` and `out` may alias.
 */
bool
vm_to_number(const Value *v, Value *out);


/**
 * @param [in, out] v
 *      As input, holds the value we wish to convert, which is only valid
 *      for numbers and strings.
 *      As output, holds the interned string representation.
 */
bool
vm_to_string(lulu_VM *L, Value *v);

const char *
vm_push_string(lulu_VM *L, LString s);

[[gnu::format(printf, 2, 3)]] const char *
vm_push_fstring(lulu_VM *L, const char *fmt, ...);

const char *
vm_push_vfstring(lulu_VM *L, const char *fmt, va_list args);

[[noreturn, gnu::format(printf, 2, 3)]] void
vm_runtime_error(lulu_VM *L, const char *fmt, ...);

void
vm_concat(lulu_VM *L, Value *ra, Slice<Value> args);

void
vm_call(lulu_VM *L, const Value *ra, int n_args, int n_rets);


/** @brief Prepares a function call for the Lua or C function at `ra`. */
Call_Type
vm_call_init(lulu_VM *L, const Value *ra, int argc, int to_return);


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
void
vm_call_fini(lulu_VM *L, Slice<Value> results);

Error
vm_load(lulu_VM *L, LString source, Stream *z);

bool
vm_table_get(lulu_VM *L, const Value *t, Value k, Value *out);

void
vm_table_set(lulu_VM *L, const Value *t, const Value *k, Value v);

void
vm_execute(lulu_VM *L, int n_calls);
