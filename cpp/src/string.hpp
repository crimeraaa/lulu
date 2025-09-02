#pragma once

#include <string.h> // strlen

#include "dynamic.hpp"
#include "private.hpp"

union Object;

using Number_Buffer = Array<char, LULU_NUMBER_BUFSIZE>;

/**
 * @param s
 *      A length-bounded string of our input. Note that for purposes of
 *      avoiding undefined behavior in `strtoul()` and `strtod()`, the
 *      underlying data MUST be nul-terminated.
 *
 * @param [out] n
 *      Holds the result of parsing the string into a number.
 *      If conversion is not successful, then the `strto[dl]` functions
 *      set it to 0.
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
lstring_to_number(LString s, Number *n, int base = 0);

LString
number_to_lstring(Number n, Slice<char> buf);

inline LString
lstring_from_cstring(const char *s)
{
    usize n = strlen(s);
    return {s, static_cast<isize>(n)};
}

inline LString
lstring_from_slice(const Slice<char> &s)
{
    return {raw_data(s), len(s)};
}

struct Builder {
    Dynamic<char> buffer;
};

// Although generally an 'independent' object, the chaining of gray strings
// is handled already by Interns so we do not need a gc_list member.
struct OString : Object_Header {
    isize len;
    u32   hash;

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

    // Prevent dubious usage; string lifetime entirely managed by VM
    // If you wish to 'extend' a string's lifetime temporarily, push it to VM
    void
    clear_fixed() = delete;
};

struct Intern {
    // Each entry in the string table is actually a linked list.
    Slice<Object *> table;
    isize           count; // Total number of strings in active use.
};

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
builder_destroy(lulu_VM *L, Builder *b);


/**
 * @note(2025-08-01)
 *      For performance, a nul character is NOT implicitly appended.
 */
void
builder_write_char(lulu_VM *L, Builder *b, char ch);

void
builder_write_lstring(lulu_VM *L, Builder *b, LString s);

void
builder_write_int(lulu_VM *L, Builder *b, int i);

void
builder_write_number(lulu_VM *L, Builder *b, Number n);

void
builder_write_pointer(lulu_VM *L, Builder *b, void *p);

void
builder_pop(Builder *b);

LString
builder_to_string(const Builder &b);

const char *
builder_to_cstring(lulu_VM *L, Builder *b);

void
intern_resize(lulu_VM *L, Intern *i, isize new_cap);

void
intern_destroy(lulu_VM *L, Intern *t);

constexpr u32 FNV1A_OFFSET = 0x811c9dc5, FNV1A_PRIME = 0x01000193;

u32
hash_string(LString text);

OString *
ostring_new(lulu_VM *L, LString text);

inline OString *
ostring_from_cstring(lulu_VM *L, const char *s)
{
    LString ls = lstring_from_cstring(s);
    return ostring_new(L, ls);
}
