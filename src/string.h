#ifndef LULU_STRING_INTERNALS_H
#define LULU_STRING_INTERNALS_H

#include "object.h"

#define luluStr_size(len)       (sizeof(String) + array_size(char, len))

uint32_t luluStr_hash(const char *cs, size_t len);

// Analogous to `allocateString()` in the book.
String *luluStr_new(lulu_VM *vm, const char *cs, size_t len, uint32_t hash);
void    luluStr_free(lulu_VM *vm, String *s);

// Global functions that deal with strings need the VM to check for interned.
String *luluStr_copy(lulu_VM *vm, const char *cs, size_t len);
#define luluStr_copy_lit(vm, s)  luluStr_copy((vm), s, cstr_len(s))

// Mutates the `vm->strings` table. Maps strings to non-nil values.
void luluStr_set_interned(lulu_VM *vm, const String *s);

// Searches for interned strings. Analogous to `tableFindString()` in the book.
String *luluStr_find_interned(lulu_VM *vm, const char *cs, size_t len, uint32_t hash);

#endif /* LULU_STRING_H */
