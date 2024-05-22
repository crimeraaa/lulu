#ifndef LULU_STRING_INTERNALS_H
#define LULU_STRING_INTERNALS_H

#include "object.h"

#define string_size(N)      (sizeof(String) + array_size(char, N))
#define string_psize(P, N)  (sizeof(*(P)) + parray_size((P)->data, N))

// Will not hash any escape sequences at all. Hence 'r' for "raw".
uint32_t hash_rstring(StrView sv);

// NOTE: For `concat_string` we do not know the correct hash yet.
// Analogous to `allocateString()` in the book.
String *new_string(int len, Alloc *al);
void free_string(String *s, Alloc *al);

// Global functions that deal with strings need the VM to check for interned.
String *copy_string(lulu_VM *vm, StrView sv);
String *copy_rstring(lulu_VM *vm, StrView sv);

// Assumes all arguments we already verified to be `String*`.
String *concat_strings(lulu_VM *vm, int argc, const Value argv[], int len);

#endif /* LULU_STRING_H */
