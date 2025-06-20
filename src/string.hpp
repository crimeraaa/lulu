#pragma once

#include <string.h>

#include "private.hpp"
#include "dynamic.hpp"
#include "value.hpp"
#include "object.hpp"

using String = Slice<const char>;

#define STRING_FMTSPEC "%.*s"
#define string_fmtarg(s) cast_int(len(s)), raw_data(s)

struct Builder
{
    Dynamic<char> buffer;
};

struct OString {
    Object base;
    size_t len;
    u32    hash;
    char   data[1];
};

using Intern_Entry = OString *;

struct Intern {
    Slice<Intern_Entry> table;
    Object             *list;
    size_t              count; // Number of slots actively used in `table`.
};

inline String
string_make(const char *cstr)
{
    size_t n = strlen(cstr);
    lulu_assert(cstr[n] == '\0');
    String s{cstr, n};
    return s;
}

inline String
string_make(const char *data, size_t len)
{
    String s{data, len};
    return s;
}

inline String
string_make(const char *start, const char *end)
{
    // The standard says we can't compare 2 pointers that point to entirely
    // different objects in memory. But for x86-64 it *can* be done.
    lulu_assert(start <= end);
    lulu_assertf(end - start >= 0, "Length of %ti less than 0", end - start);
    String s{start, cast(size_t, end - start)};
    return s;
}

inline String
string_make(Slice<char> buffer)
{
    String s{raw_data(buffer), len(buffer)};
    return s;
}

inline String
string_slice(String s, size_t start, size_t stop)
{
    // Ensure the resulting length would not cause unsigned overflow.
    lulu_assert(start <= stop);
    String s2{&s[start], stop - start};
    return s2;
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
intern_destroy(lulu_VM &vm, Intern &t);

OString *
ostring_new(lulu_VM &vm, String text);

void
ostring_free(lulu_VM &vm, OString *o);
