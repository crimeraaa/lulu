#ifndef LULU_STRING_H
#define LULU_STRING_H

#include "lulu.h"

/**
 * @brief
 *      A read-only view into some characters.
 * 
 * @note 2024-09-04
 *      The underlying buffer may not necessarily be nul terminated!
 */
typedef struct {
    const char *data;
    isize       len;
} lulu_String_View;

/**
 * @brief
 *      Construct a String from a C-string literal. This is designed to work for
 *      both declaration + assignment and post-declaration assignment.
 * 
 * @warning 2024-09-07
 *      C99-style compound literals have very different semantics in C++.
 *      "Struct literals" are valid (in C++) due to implicit copy constructors.
 */
#define lulu_String_View_literal(cstr) {(cstr), size_of(cstr) - 1}

/**
 * @brief
 *      1D heap-allocated dynamic array of `char`.
 *
 * @todo 2024-09-06
 *      Until we are able to intern strings, we won't be using this yet!
 *      This will however be useful during lexing when we want to deal with
 *      escape characters and multiline strings.
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
lulu_String_Builder_write_char(lulu_String_Builder *self, char ch);

void
lulu_String_Builder_write_string(lulu_String_Builder *self, lulu_String_View str);

void
lulu_String_Builder_write_cstring(lulu_String_Builder *self, cstring cstr);


#endif // LULU_STRING_H
