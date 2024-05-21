#ifndef LULU_STRING_INTERNALS_H
#define LULU_STRING_INTERNALS_H

#include "object.h"

#define string_size(N)      (sizeof(String) + array_size(char, N))
#define string_psize(P, N)  (sizeof(*(P)) + parray_size((P)->data, N))

// This function will not hash any escape sequences at all.
uint32_t hash_rstring(StrView view);

// NOTE: For `concat_string` we do not know the correct hash yet.
// Analogous to `allocateString()` in the book.
String *new_string(int len, Alloc *alloc);
void free_string(String *self, Alloc *alloc);

// Global functions that deal with strings need the VM to check for interned.
String *copy_string(VM *vm, StrView view);
String *copy_rstring(VM *vm, StrView view);

// Assumes all arguments we already verified to be `String*`.
String *concat_strings(VM *vm, int argc, const Value argv[], int len);

#endif /* LULU_STRING_H */
