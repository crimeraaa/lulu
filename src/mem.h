#pragma once

#include "lulu.h"
#include "private.h"

size_t
mem_next_pow2(size_t n);

void *
mem_realloc(lulu_VM *vm, void *ptr, size_t old_size, size_t new_size);

// Raw Memory 'Functions'
#define mem_raw_new(vm, size)              mem_realloc(vm, nullptr, 0, size)
#define mem_raw_free(vm, ptr, size)        mem_realloc(vm, ptr, size, 0)
#define mem_raw_resize(vm, type_size, ptr, prev, next) \
    mem_realloc(vm, ptr, (type_size) * (prev), (type_size) * (next))

#define mem_raw_delete(vm, type_size, ptr, count) \
    mem_raw_free(vm, ptr, (type_size) * (count))

// Typed Memory 'Functions'
#define mem_resize(vm, T, ptr, prev, next) \
    cast(T *, mem_raw_resize(vm, sizeof(T), ptr, prev, next))

#define mem_delete(vm, ptr, count) \
    mem_raw_delete(vm, sizeof(*(ptr)), ptr, count)
