#ifndef LULU_STRING_H
#define LULU_STRING_H

#include "lulu.h"
#include "object.h"

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
} String;

/**
 * @brief
 *      Construct a String from a C-string literal. This is designed to work for
 *      both declaration + assignment and post-declaration assignment.
 * 
 * @warning 2024-09-07
 *      C99-style compound literals have very different semantics in C++.
 *      "Struct literals" are valid (in C++) due to implicit copy constructors.
 */
#define String_literal(cstr) {(cstr), size_of(cstr) - 1}

struct lulu_String {
    lulu_Object base;
    isize       len;    // Number of non-nul characters.
    char        data[]; // Guaranteed to be nul terminated.
};

lulu_String *
lulu_String_new(lulu_VM *vm, String src);

void
lulu_String_free(lulu_VM *vm, lulu_String *self);

lulu_String *
lulu_String_concat(lulu_VM *vm, lulu_String *a, lulu_String *b);

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
} String_Builder;

void
String_Builder_init(lulu_VM *vm, String_Builder *self);

void
String_Builder_reserve(String_Builder *self, isize new_cap);

void
String_Builder_free(String_Builder *self);

void
String_Builder_write_char(String_Builder *self, char ch);

void
String_Builder_write_string(String_Builder *self, String str);

void
String_Builder_write_cstring(String_Builder *self, cstring cstr);


#endif // LULU_STRING_H
