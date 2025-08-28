#pragma once

#include <stdlib.h> // exit

#include "object.hpp"
#include "private.hpp"
#include "stream.hpp"

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

    // Linked list of all collectable objects.
    Object *objects;

    // Hack until we get proper GC states.
    bool gc_paused;
};

// Does NOT use VM global allocator to prevent recursive GC collection.
template<class T>
struct CDynamic {
    T    *data;
    isize len;
    isize cap;

    T &
    operator[](isize i)
    {
        lulu_assert(0 <= i && i < this->len);
        return this->data[i];
    }
};

template<class T>
inline void
cdynamic_push(CDynamic<T> *d, T v)
{
    if (d->len + 1 > d->cap) {
        d->cap  = mem_next_pow2(max(d->len + 1, 8_i));
        d->data = static_cast<T *>(realloc(d->data, sizeof(T) * d->cap));
        // @todo(2025-08-27) Do literally anything else
        if (d->data == nullptr) {
            exit(1);
        }
    }
    d->data[d->len++] = v;
}

template<class T>
inline T
cdynamic_pop(CDynamic<T> *d)
{
    // Decrement must occur after access to avoid tripping up bounds check.
    isize i = d->len - 1;
    T top = (*d)[i];
    d->len = i;
    return top;
}

template<class T>
inline void
cdynamic_delete(CDynamic<T> &d)
{
    free(d.data);
}

struct LULU_PUBLIC lulu_VM {
    lulu_Global       *global_state;
    Stack_Array        stack;
    Frame_Array        frames;
    Call_Frame        *caller; // Not a reference because it can be reassigned.
    Slice<Value>       window;
    Value              globals;
    Error_Handler     *error_handler;
    const Instruction *saved_ip; // Used for error handling.

    // Linked list of open upvalues in the current stack frame.
    // Helps with variable reuse.
    Object *open_upvalues;

    CDynamic<Object *> gray_stack;

    LULU_PRIVATE
    lulu_VM() = default;
};


inline lulu_Global *
G(lulu_VM *L)
{
    return L->global_state;
}

using Protected_Fn = void (*)(lulu_VM *L, void *user_ptr);

Builder *
vm_get_builder(lulu_VM *L);

Value *
vm_base_ptr(lulu_VM *L);

Value *
vm_top_ptr(lulu_VM *L);


/**
 * @brief
 *      Gets the absolute index of `v` in `L->stack`.
 */
int
vm_absindex(lulu_VM *L, const Value *v);

int
vm_base_absindex(lulu_VM *L);

int
vm_top_absindex(lulu_VM *L);


/**
 * @brief
 *      Wraps a call to `fn(L, user_ptr)` with a try-catch block. In case
 *      of errors, the error message, as a string, is left on the top of
 *      the stack.
 */
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


/**
 * @brief
 *      Prepares a function call for the Lua or C function at `ra`.
 *      If it is a C function, it is called directly. Otherwise,
 *      if it is a Lua function, it can be called by `vm_execute()`.
 */
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
