#ifndef LULU_H
#define LULU_H

#include <stdarg.h>
#include <stddef.h>

#include "config.h"

typedef struct lulu_VM lulu_VM;

typedef LULU_NUMBER_TYPE lulu_Number;

typedef void *
(*lulu_Allocator)(void *context, void *ptr, size_t old_size, size_t new_size);


/**
 * @param argc
 *  -   Number of arguments pushed to the stack for this function call.
 *  -   If zero, then index 1 is free.
 *  -   If non-zero, then index 1 up to and including `argc` is occupied.
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
    LULU_ERROR_MEMORY,
} lulu_Error;


/**
 * @brief 2025-06-16
 *  -   Chapter 18.1 of Crafting Interpreters: "Tagged Unions".
 */
typedef enum {
    LULU_TYPE_NONE = -1,     /* out of bounds stack index, C API use only. */
    LULU_TYPE_NIL,
    LULU_TYPE_BOOLEAN,
    LULU_TYPE_NUMBER,
    LULU_TYPE_STRING,
    LULU_TYPE_TABLE,
    LULU_TYPE_FUNCTION,
} lulu_Type;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

lulu_VM *
lulu_open(lulu_Allocator allocator, void *allocator_data);

void
lulu_close(lulu_VM *vm);


/**
 * @brief
 *  -   Compiles the string `script` into a Lua function, which is pushed to
 *      the top of the stack.
 */
lulu_Error
lulu_load(lulu_VM *vm, const char *source, const char *script, size_t script_size);


/**
 * @brief
 *  -   Calls the Lua or C function at the stack index `-(n_args + 1)` and
 *      adjusts the current VM call frame by `n_rets`.
 */
void
lulu_call(lulu_VM *vm, int n_args, int n_rets);


/**
 * @brief
 *  -   Wraps `lulu_call()` in a protected call so that we may catch any thrown
 *      exceptions.
 */
lulu_Error
lulu_pcall(lulu_VM *vm, int n_args, int n_rets);

/*=== TYPE QUERY FUNCTIONS ============================================== {{{ */


lulu_Type
lulu_type(lulu_VM *vm, int i);

const char *
lulu_type_name(lulu_VM *vm, int i);

int
lulu_is_nil(lulu_VM *vm, int i);

int
lulu_is_boolean(lulu_VM *vm, int i);

int
lulu_is_number(lulu_VM *vm, int i);

int
lulu_is_string(lulu_VM *vm, int i);

int
lulu_is_table(lulu_VM *vm, int i);

int
lulu_is_function(lulu_VM *vm, int i);

bool
lulu_to_boolean(lulu_VM *vm, int i);

lulu_Number
lulu_to_number(lulu_VM *vm, int i);

const char *
lulu_to_string(lulu_VM *vm, int i, size_t *n);

#define lulu_to_cstring(vm, i)  lulu_to_string(vm, i, NULL)

void *
lulu_to_pointer(lulu_VM *vm, int i);

/*=== }}} =================================================================== */


void
lulu_set_top(lulu_VM *vm, int i);

void
lulu_pop(lulu_VM *vm, int n);

void
lulu_push_nil(lulu_VM *vm, int n);

void
lulu_push_boolean(lulu_VM *vm, int b);

void
lulu_push_number(lulu_VM *vm, lulu_Number n);

void
lulu_push_string(lulu_VM *vm, const char *s, size_t n);

[[gnu::format(printf, 2, 3)]]
const char *
lulu_push_fstring(lulu_VM *vm, const char *fmt, ...);

const char *
lulu_push_vfstring(lulu_VM *vm, const char *fmt, va_list args);

void
lulu_push_cfunction(lulu_VM *vm, lulu_CFunction cf);

#define lulu_push_literal(vm, s)    lulu_push_string(vm, s, sizeof(s) - 1)

void
lulu_set_global(lulu_VM *vm, const char *s);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* LULU_H */
