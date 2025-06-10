#include "mem.h"

#ifndef DYNAMIC_TYPE
#error Need an explicit type to instantiate `Dynamic` with.

// lol
#ifdef LULU_DYNAMIC_LSP_HACK
#define DYNAMIC_TYPE    double
#define DYNAMIC_IMPLEMENTATION
#endif

#endif // DYNAMIC_TYPE

#define T DYNAMIC_TYPE

#define _dynamic_expand(prefix, suffix)  prefix ##_ ##suffix
#define Dynamic(T)  _dynamic_expand(Dynamic, T)

typedef struct {
    T     *data;
    size_t len;
    size_t cap;
} Dynamic(T);

#define _dynamic_method(T, act) _dynamic_expand(dynamic, T ##_ ##act)
#define dynamic_init(T)         _dynamic_method(T, init)
#define dynamic_push(T)         _dynamic_method(T, push)
#define dynamic_reserve(T)      _dynamic_method(T, resize)
#define dynamic_delete(T)       _dynamic_method(T, delete)
#define dynamic_get(T)          _dynamic_method(T, get)
#define dynamic_get_ptr(T)      _dynamic_method(T, get_ptr)

// State Management
void
dynamic_init(T)(Dynamic(T) *d);

// Value Manipulation
void
dynamic_push(T)(lulu_VM *vm, Dynamic(T) *d, T value);

// Memory Management
void
dynamic_reserve(T)(lulu_VM *vm, Dynamic(T) *d, size_t cap);

void
dynamic_delete(T)(lulu_VM *vm, Dynamic(T) *d);

T
dynamic_get(T)(Dynamic(T) *d, size_t i);

T *
dynamic_get_ptr(T)(Dynamic(T) *d, size_t i);

#ifdef DYNAMIC_IMPLEMENTATION

// State Management
void
dynamic_init(T)(Dynamic(T) *d)
{
    d->data = nullptr;
    d->len  = 0;
    d->cap  = 0;
}

// Value Manipulation
void
dynamic_push(T)(lulu_VM *vm, Dynamic(T) *d, T value)
{
    if (d->len >= d->cap) {
        size_t next = mem_next_pow2(d->cap);
        dynamic_reserve(T)(vm, d, next);
    }
    d->data[d->len++] = value;
}

// Memory Management
void
dynamic_reserve(T)(lulu_VM *vm, Dynamic(T) *d, size_t cap)
{
    d->data = mem_resize(vm, T, d->data, d->cap, cap);
    d->cap  = cap;
}

void
dynamic_delete(T)(lulu_VM *vm, Dynamic(T) *d)
{
    mem_delete(vm, d->data, d->cap);
    // Reset to avoid dangling pointers in case `d` is reused
    dynamic_init(T)(d);
}

T
dynamic_get(T)(Dynamic(T) *d, size_t i)
{
    return *dynamic_get_ptr(T)(d, i);
}

T *
dynamic_get_ptr(T)(Dynamic(T) *d, size_t i)
{
    if (0 <= i && i < d->len) {
        return &d->data[i];
    }
    __builtin_trap();
}

#endif // DYNAMIC_IMPLEMENTATION

#undef T
#undef DYNAMIC_TYPE
