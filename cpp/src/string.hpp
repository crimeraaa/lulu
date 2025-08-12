#pragma once

#include <string.h> // strlen

#include "private.hpp"
#include "dynamic.hpp"

union Object;

using LString = Slice<const char>;

using Number_Buffer = Array<char, LULU_NUMBER_BUFSIZE>;

/**
 * @param s
 *      A length-bounded string of our input. Note that for purposes of
 *      avoiding undefined behavior in `strtoul()` and `strtod()`, the
 *      underlying data MUST be nul-terminated.
 *
 * @param base
 *      If zero, tries to detect any potential base prefixes in the form of the
 *      regular expression `^0[bBoOdDxX]`.
 *
 * @return
 *      `true` if `s` was successfully parsed into a `Number` and `*out` was
 *      assigned, otherwise `false`.
 */
bool
lstring_to_number(LString s, Number *out, int base = 0);

LString
number_to_lstring(Number n, Slice<char> buf);

inline LString
lstring_from_cstring(const char *s)
{
    usize n = strlen(s);
    return {s, cast_isize(n)};
}

inline LString
lstring_from_slice(const Slice<char> &s)
{
    return {raw_data(s), len(s)};
}

struct Builder {
    Dynamic<char> buffer;
};

struct OString {
    OBJECT_HEADER;
    isize len;
    u32 hash;

    // Used only by Lexer when resolving keywords.
    // Must hold a `Token_Type`.
    i8 keyword_type;

    // Flexible array member, actual length is determined by `len`.
    char data[1];

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

struct Intern {
    // Each entry in the string table is actually a linked list.
    Slice<Object *> table;
    isize           count; // Total number of strings in active use.
};

inline LString
operator ""_s(const char *s, size_t n)
{
    return {s, cast_isize(n)};
}

#define lstring_literal(lit)    TOKEN_PASTE(lit, _s)

void
builder_init(Builder *b);

inline isize
builder_len(const Builder &b)
{
    return len(b.buffer);
}

inline isize
builder_cap(const Builder &b)
{
    return cap(b.buffer);
}

void
builder_reset(Builder *b);

void
builder_destroy(lulu_VM *vm, Builder *b);


/**
 * @note(2025-08-01)
 *      For performance, a nul character is NOT implicitly appended.
 */
void
builder_write_char(lulu_VM *vm, Builder *b, char ch);

void
builder_write_lstring(lulu_VM *vm, Builder *b, LString s);

void
builder_write_int(lulu_VM *vm, Builder *b, int i);

void
builder_write_number(lulu_VM *vm, Builder *b, Number n);

void
builder_write_pointer(lulu_VM *vm, Builder *b, void *p);

void
builder_pop(Builder *b);

LString
builder_to_string(const Builder &b);

const char *
builder_to_cstring(lulu_VM *vm, Builder *b);

void
intern_init(Intern *t);

void
intern_resize(lulu_VM *vm, Intern *i, isize new_cap);

void
intern_destroy(lulu_VM *vm, Intern *t);

u32
hash_string(LString text);

OString *
ostring_new(lulu_VM *vm, LString text);

inline OString *
ostring_from_cstring(lulu_VM *vm, const char *s)
{
    LString ls = lstring_from_cstring(s);
    return ostring_new(vm, ls);
}
