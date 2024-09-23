#ifndef LULU_BUFFER_H
#define LULU_BUFFER_H

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
} lulu_Builder;

void
lulu_Builder_init(lulu_VM *vm, lulu_Builder *self);

void
lulu_Builder_reserve(lulu_Builder *self, isize new_cap);

void
lulu_Builder_free(lulu_Builder *self);

void
lulu_Builder_reset(lulu_Builder *self);

void
lulu_Builder_write_char(lulu_Builder *self, char ch);

void
lulu_Builder_write_string(lulu_Builder *self, const char *data, isize len);

void
lulu_Builder_write_cstring(lulu_Builder *self, cstring cstr);

const char *
lulu_Builder_to_string(lulu_Builder *self, isize *out_len);

#endif // LULU_BUFFER_H
