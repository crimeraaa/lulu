#pragma once

#include <string.h> // strlen

#include "private.hpp"
#include "dynamic.hpp"

union Object;

using LString = Slice<const char>;

LULU_FUNC inline LString
lstring_from_cstring(const char *s)
{
    usize n = strlen(s);
    return {s, cast_isize(n)};
}

LULU_FUNC inline LString
lstring_from_slice(Slice<char> s)
{
    return {raw_data(s), len(s)};
}

struct LULU_PRIVATE Builder {
    Dynamic<char> buffer;
};

struct LULU_PRIVATE OString {
    OBJECT_HEADER;
    isize len;
    u32   hash;
    char  data[1];

    LString
    to_lstring() const noexcept
    {
        return {this->data, this->len};
    }

    const char *
    to_cstring() const
    {
        lulu_assert(this->data[this->len] == '\0');
        return this->data;
    }
};

struct LULU_PRIVATE Intern {
    // Each entry in the string table is actually a linked list.
    Slice<Object *> table;
    isize           count; // Total number of strings in active use.
};

LULU_FUNC inline LString
operator ""_s(const char *s, size_t n)
{
    return {s, cast_isize(n)};
}

LULU_FUNC void
builder_init(Builder *b);

LULU_FUNC isize
builder_len(const Builder *b);

LULU_FUNC isize
builder_cap(const Builder *b);

LULU_FUNC void
builder_reset(Builder *b);

LULU_FUNC void
builder_destroy(lulu_VM *vm, Builder *b);

LULU_FUNC void
builder_write_lstring(lulu_VM *vm, Builder *b, LString s);

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
intern_resize(lulu_VM *vm, Intern *i, isize new_cap);

LULU_FUNC void
intern_destroy(lulu_VM *vm, Intern *t);

LULU_FUNC u32
hash_string(LString text);

LULU_FUNC OString *
ostring_new(lulu_VM *vm, LString text);
