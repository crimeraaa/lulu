#pragma once

#include <string.h> // strlen

#include "private.hpp"
#include "dynamic.hpp"
#include "object.hpp"

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
builder_destroy(lulu_VM &vm, Builder &b);

void
builder_write_char(lulu_VM &vm, Builder &b, char ch);

void
builder_write_string(lulu_VM &vm, Builder &b, String s);

String
builder_to_string(const Builder &b);

void
intern_init(Intern &t);

void
intern_resize(lulu_VM &vm, Intern &i, size_t new_cap);

void
intern_destroy(lulu_VM &vm, Intern &t);

OString *
ostring_new(lulu_VM &vm, String text);

inline String
ostring_to_string(OString *s)
{
    return String(s->data, s->len);
}
