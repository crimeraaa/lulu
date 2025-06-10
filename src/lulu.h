#pragma once

#include <stddef.h>

#include "config.h"

typedef struct lulu_VM lulu_VM;

typedef LULU_NUMBER_TYPE lulu_Number;

typedef void *
(*lulu_Allocator)(void *context, void *ptr, size_t old_size, size_t new_size);

