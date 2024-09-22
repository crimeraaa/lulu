#ifndef LULU_STRING_H
#define LULU_STRING_H

#include "object.h"

#if defined __GNUC__
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wc99-extensions"
#endif

struct lulu_String {
    lulu_Object base;
    isize       len;    // Number of non-nul characters.
    u32         hash;
    char        data[]; // Guaranteed to be nul terminated.
};

#if defined __GNUC__
    #pragma GCC diagnostic pop
#endif

void
lulu_String_init(lulu_String *self, isize len, u32 hash);

// Analogous to the book's object.c:allocateString().
lulu_String *
lulu_String_new(lulu_VM *vm, const char *data, isize len);

// Since self is a variable length structure pointer, we free it here.
void
lulu_String_free(lulu_VM *vm, lulu_String *self);

lulu_String *
lulu_String_concat(lulu_VM *vm, lulu_String *a, lulu_String *b);

u32
lulu_String_hash(const char *src, isize len);

/**
 * @brief
 *      1D heap-allocated dynamic array of `char`.
 */
typedef struct {
    lulu_VM *vm;     // Parent/enclosing state which contains our allocator.
    char    *buffer; // Dynamically growable array.
    isize    len;    // Number of currently active elements.
    isize    cap;    // Number of allocated elements.
} lulu_String_Builder;

void
lulu_String_Builder_init(lulu_VM *vm, lulu_String_Builder *self);

void
lulu_String_Builder_reserve(lulu_String_Builder *self, isize new_cap);

void
lulu_String_Builder_free(lulu_String_Builder *self);

void
lulu_String_Builder_reset(lulu_String_Builder *self);

void
lulu_String_Builder_write_char(lulu_String_Builder *self, char ch);

void
lulu_String_Builder_write_string(lulu_String_Builder *self, const char *data, isize len);

void
lulu_String_Builder_write_cstring(lulu_String_Builder *self, cstring cstr);

lulu_String *
lulu_String_Builder_to_string(lulu_String_Builder *self);

#endif // LULU_STRING_H
