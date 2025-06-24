#ifndef LULU_H
#define LULU_H

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
    LULU_TYPE_NIL,
    LULU_TYPE_BOOLEAN,
    LULU_TYPE_NUMBER,
    LULU_TYPE_STRING,
    LULU_TYPE_TABLE,
    LULU_TYPE_FUNCTION,
} lulu_Type;

#endif /* LULU_H */
