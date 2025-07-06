#pragma once

#include <string.h> // strlen

#include "private.hpp"
#include "dynamic.hpp"

union Object;

using LString = Slice<const char>;

LULU_FUNC inline LString
lstring_from_cstring(const char *s)
{
    return {s, strlen(s)};
}

LULU_FUNC inline LString
lstring_from_slice(Slice<char> s)
{
    return {raw_data(s), len(s)};
}

struct Builder {
    Dynamic<char> buffer;
};

struct OString {
    OBJECT_HEADER;
    size_t len;
    u32    hash;
    char   data[1];
};

struct Intern {
    // Each entry in the string table is actually a linked list.
    Slice<Object *> table;
    size_t          count; // Total number of strings in active use.
};

LULU_FUNC inline bool
operator==(LString a, LString b)
{
    return slice_eq(a, b);
}

LULU_FUNC inline LString
operator ""_s(const char *s, size_t n)
{
    return {s, n};
}

LULU_FUNC void
builder_init(Builder *b);

LULU_FUNC size_t
builder_len(const Builder *b);

LULU_FUNC size_t
builder_cap(const Builder *b);

LULU_FUNC void
builder_reset(Builder *b);

LULU_FUNC void
builder_destroy(lulu_VM *vm, Builder *b);

LULU_FUNC void
builder_write_string(lulu_VM *vm, Builder *b, LString s);

LULU_FUNC void
builder_write_char(lulu_VM *vm, Builder *b, char ch);

LULU_FUNC void
builder_write_int(lulu_VM *vm, Builder *b, int i);

LULU_FUNC void
builder_write_number(lulu_VM *vm, Builder *b, Number n);

LULU_FUNC void
builder_write_pointer(lulu_VM *vm, Builder *b, void *p);

LULU_FUNC LString
builder_to_string(const Builder *b);

LULU_FUNC const char *
builder_to_cstring(const Builder *b);

LULU_FUNC void
intern_init(Intern *t);

LULU_FUNC void
intern_resize(lulu_VM *vm, Intern *i, size_t new_cap);

LULU_FUNC void
intern_destroy(lulu_VM *vm, Intern *t);

LULU_FUNC u32
hash_string(LString text);

LULU_FUNC OString *
ostring_new(lulu_VM *vm, LString text);

LULU_FUNC inline LString
ostring_to_string(OString *s)
{
    return {s->data, s->len};
}

LULU_FUNC inline const char *
ostring_to_cstring(OString *s)
{
    lulu_assert(s->data[s->len] == '\0');
    return s->data;
}
