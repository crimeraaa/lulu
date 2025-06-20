#pragma once

#include <stddef.h>

#include "config.h"

typedef struct lulu_VM lulu_VM;

typedef LULU_NUMBER_TYPE lulu_Number;

typedef void *
(*lulu_Allocator)(void *context, void *ptr, size_t old_size, size_t new_size);

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
} lulu_Type;
