#ifndef LULU_BUILDER_H
#define LULU_BUILDER_H

#include "lulu.h"
#include "string.h"

/**
 * @brief
 *      A string builder, which is a 1D, heap-allocated, dynamic 'char' array.
 */
typedef struct {
    lulu_VM *vm;     // Parent/enclosing state which contains our allocator.
    char    *buffer; // Dynamically growable array.
    isize    len;    // Number of currently active elements.
    isize    cap;    // Number of allocated elements.
} Builder;

void
builder_init(lulu_VM *vm, Builder *self);

void
builder_reserve(Builder *self, isize new_cap);

void
builder_free(Builder *self);

void
builder_reset(Builder *self);

void
builder_write_char(Builder *self, char ch);

void
builder_write_string(Builder *self, const char *data, isize len);

void
builder_write_cstring(Builder *self, cstring cstr);

const char *
builder_to_string(Builder *self, isize *out_len);

#endif // LULU_BUILDER_H
