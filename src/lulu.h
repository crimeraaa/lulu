#ifndef LULU_H
#define LULU_H

#include <stdarg.h>
#include <stddef.h>

#include "config.h"

typedef struct lulu_VM lulu_VM;
typedef LULU_NUMBER_TYPE lulu_Number;


/**
 * @brief LULU_API
 *  -   Defines the name linkage for functions.
 *  -   C++ by default uses 'name-mangling', which allows multiple declarations
 *      of the same function name to have different parameter lists.
 *  -   Name mangling makes it impossible for C relilably link to C++.
 *  -   So using `extern "C"` specifies that, when compiling with a C++
 *      compiler, no name mangling should occur.
 *  -   Thus C programs can properly link to our shared library.
 */
#ifdef __cplusplus
#define LULU_API    extern "C" LULU_PUBLIC
#else   /* ^^^ __cplusplus, vvv otherwise */
#define LULU_API    extern LULU_PUBLIC
#endif /* __cplusplus */


/**
 * @brief LULU_FUNC
 * -    This controls the visibility of externally visible functions that are
 *      not part of the API.
 * -    That is, you can define a function like so: `LULU_FUNC void f();`
 *  -   `f` will not be exported but it is still visible to all functions
 *      within the library that include `f`'s header.
 *
 * @brief LULU_DATA
 *  -   Similar to `LULU_FUNC`, but intended for data.
 *  -   e.g. `LULU_DATA const char *const tokens[TOKEN_COUNT];`
 */
#define LULU_FUNC       extern LULU_PRIVATE
#define LULU_DATA       LULU_FUNC

typedef void *
(*lulu_Allocator)(void *context, void *ptr, size_t old_size, size_t new_size);


/**
 * @param argc
 *  -   Number of arguments pushed to the stack for this function call.
 *  -   If zero, then no stack slots are currently occupied by any arguments.
 *  -   If non-zero, then index 1 up to and including `argc` are occupied.
 *  -   You may choose to ignore it.
 *
 * @return
 *  -   The number of values pushed to the stack that will be used by the
 *      caller.
 */
typedef int
(*lulu_CFunction)(lulu_VM *vm, int argc);


/**
 * @brief 2025-06-11
 *  -   Chapter 15.1.1 of Crafting Interpreters: "Executing instructions".
 */
typedef enum {
    LULU_OK,
    LULU_ERROR_SYNTAX,
    LULU_ERROR_RUNTIME,
    LULU_ERROR_MEMORY
} lulu_Error;


/**
 * @brief 2025-06-16
 *  -   Chapter 18.1 of Crafting Interpreters: "Tagged Unions".
 */
typedef enum {
    LULU_TYPE_NONE = -1,    /* out of bounds stack index, C API only. */
    LULU_TYPE_NIL,
    LULU_TYPE_BOOLEAN,
    LULU_TYPE_NUMBER,
    LULU_TYPE_USERDATA,     /* a non-collectible C pointer. */
    LULU_TYPE_STRING,
    LULU_TYPE_TABLE,
    LULU_TYPE_FUNCTION      /* may be a Lua or C function. */
} lulu_Type;

typedef struct {
    const char    *name;
    lulu_CFunction function;
} lulu_Register;

LULU_API lulu_VM *
lulu_open(lulu_Allocator allocator, void *allocator_data);

LULU_API void
lulu_close(lulu_VM *vm);


/**
 * @brief
 *  -   Compiles the string `script` into a Lua function, which is pushed to
 *      the top of the stack.
 */
LULU_API lulu_Error
lulu_load(lulu_VM *vm, const char *source, const char *script, size_t script_size);


/**
 * @brief
 *  -   Calls the Lua or C function at the stack index `-(n_args + 1)`
 *      with arguments from `(nargs)` up to and including `-1`.
 *
 * @note 2025-06-30
 *  -   The function and its arguments are popped when done.
 *  -   The return values, if any, are moved to where the function originally
 *      was.
 *  -   That is, the `-(n_args + 1)` stack slot, up to the `n_ret` slot above
 *      it, is overwritten.
 *  -   If the function returned less than `n_rets` values then remaining
 *      slots are set to `nil`.
 */
LULU_API void
lulu_call(lulu_VM *vm, int n_args, int n_rets);


/**
 * @brief
 *  -   Wraps `lulu_call()` in a protected call so that we may catch any thrown
 *      exceptions.
 */
LULU_API lulu_Error
lulu_pcall(lulu_VM *vm, int n_args, int n_rets);


/**
 * @brief
 *  -   Wraps the call `function(vm, function_data)` in a protected call
 *      so that we may catch any thrown exceptions.
 */
LULU_API lulu_Error
lulu_c_pcall(lulu_VM *vm, lulu_CFunction function, void *function_data);


/**
 * @brief
 *  -   Throws a runtime error no matter what.
 *
 * @note 2025-07-01
 *  -   This function never returns, but as in the Lua 5.1 API a common idiom
 *      within C functions is to do `return lulu_error();`
 */
LULU_API int
lulu_error(lulu_VM *vm);

LULU_API void
lulu_register(lulu_VM *vm, const lulu_Register *library, size_t n);

/*=== TYPE QUERY FUNCTIONS ============================================== {{{ */


LULU_API lulu_Type
lulu_type(lulu_VM *vm, int i);

LULU_API const char *
lulu_type_name(lulu_VM *vm, int i);

LULU_API int
lulu_is_none(lulu_VM *vm, int i);

LULU_API int
lulu_is_nil(lulu_VM *vm, int i);

LULU_API int
lulu_is_boolean(lulu_VM *vm, int i);

LULU_API int
lulu_is_number(lulu_VM *vm, int i);

LULU_API int
lulu_is_userdata(lulu_VM *vm, int i);

LULU_API int
lulu_is_string(lulu_VM *vm, int i);

LULU_API int
lulu_is_table(lulu_VM *vm, int i);

LULU_API int
lulu_is_function(lulu_VM *vm, int i);

LULU_API int
lulu_to_boolean(lulu_VM *vm, int i);

LULU_API lulu_Number
lulu_to_number(lulu_VM *vm, int i);

LULU_API const char *
lulu_to_lstring(lulu_VM *vm, int i, size_t *n);

LULU_API void *
lulu_to_pointer(lulu_VM *vm, int i);

/*=== }}} =================================================================== */

/*=== STACK MANIPULATION FUNCTIONS ====================================== {{{ */

LULU_API void
lulu_set_top(lulu_VM *vm, int i);

LULU_API void
lulu_pop(lulu_VM *vm, int n);

LULU_API void
lulu_push_nil(lulu_VM *vm, int n);

LULU_API void
lulu_push_boolean(lulu_VM *vm, int b);

LULU_API void
lulu_push_number(lulu_VM *vm, lulu_Number n);

LULU_API void
lulu_push_userdata(lulu_VM *vm, void *p);

LULU_API void
lulu_push_lstring(lulu_VM *vm, const char *s, size_t n);

LULU_API void
lulu_push_string(lulu_VM *vm, const char *s);

LULU_API const char *
#if defined(__GNUC__) || defined(__clang__)
__attribute__ ((format (printf, 2, 3)))
#endif
lulu_push_fstring(lulu_VM *vm, const char *fmt, ...);

LULU_API const char *
lulu_push_vfstring(lulu_VM *vm, const char *fmt, va_list args);

LULU_API void
lulu_push_cfunction(lulu_VM *vm, lulu_CFunction cf);


/**
 * @brief
 *  -   Pushes a copy of the value at stack index `i` to the top of the stack.
 */
LULU_API void
lulu_push_value(lulu_VM *vm, int i);

/*=== }}} =================================================================== */


/**
 * @brief
 *  -   Gets the key `s` from the VM globals table and pushes it to the current
 *      top of the stack.
 *
 * @return
 *  -   `0` if `s` does not exist in the globals table, else `1`.
 */
LULU_API int
lulu_get_global(lulu_VM *vm, const char *s);


/**
 * @brief
 *  -   Sets the global variable with key `s` to the current top of the stack.
 *  -   The value is popped.
 */
LULU_API void
lulu_set_global(lulu_VM *vm, const char *s);


/**
 * @brief
 *  -   Perform string concatenation on the `-(n)` up to and including `-1`
 *      stack indexes.
 *  -   The `-(n)`th slot is overwritten with the result.
 *
 * @note 2025-06-30
 *  Assumptions:
 *  1.) The stack has at least `n` elements such that doing pointer arithmetic
 *      is vaild.
 */
LULU_API void
lulu_concat(lulu_VM *vm, int n);

#define lulu_to_string(vm, i)       lulu_to_lstring(vm, i, NULL)
#define lulu_push_literal(vm, s)    lulu_push_lstring(vm, s, sizeof(s) - 1)

#endif /* LULU_H */
