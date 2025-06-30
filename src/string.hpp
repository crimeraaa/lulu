#pragma once

#include <string.h> // strlen

#include "private.hpp"
#include "dynamic.hpp"

union Object;

struct String : public Slice<const char> {
    // Bring all inherited constructors into scope.
    using Slice<const char>::Slice;

    // Construct from a nul-terminated C string.
    String(const char *s) : Slice(s, strlen(s))
    {}

    // Construct from a mutable character sequence.
    String(Slice<char> s) : Slice(s.data, s.len)
    {}
};

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

inline bool
operator==(String a, String b)
{
    return slice_eq(a, b);
}

inline String
operator ""_s(const char *s, size_t n)
{
    return String(s, n);
}

void
builder_init(Builder &b);

size_t
builder_len(const Builder &b);

size_t
builder_cap(const Builder &b);

void
builder_reset(Builder &b);

void
builder_destroy(lulu_VM *vm, Builder &b);

void
builder_write_char(lulu_VM *vm, Builder &b, char ch);

void
builder_write_string(lulu_VM *vm, Builder &b, String s);

void
builder_write_int(lulu_VM *vm, Builder &b, int i);

String
builder_to_string(const Builder &b);

const char *
builder_to_cstring(const Builder &b);

void
intern_init(Intern &t);

void
intern_resize(lulu_VM *vm, Intern &i, size_t new_cap);

void
intern_destroy(lulu_VM *vm, Intern &t);

u32
hash_string(String text);

OString *
ostring_new(lulu_VM *vm, String text);

inline String
ostring_to_string(OString *s)
{
    return String(s->data, s->len);
}

inline const char *
ostring_to_cstring(OString *s)
{
    lulu_assert(s->data[s->len] == '\0');
    return s->data;
}
