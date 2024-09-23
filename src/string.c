/// local
#include "string.h"
#include "table.h"
#include "vm.h"

/// standard
#include <string.h> // memcpy, memset

#define size_of_string(len)     (size_of(lulu_String) + size_of(char) * ((len) + 1))

void
lulu_String_init(lulu_String *self, isize len, u32 hash)
{
    self->len       = len;
    self->hash      = hash;
    self->data[len] = '\0';
}

lulu_String *
lulu_String_new(lulu_VM *vm, const char *data, isize len)
{
    const u32    hash     = lulu_String_hash(data, len);
    lulu_String *interned = lulu_Table_find_string(&vm->strings, data, len, hash);
    if (interned) {
        return interned;
    }
    lulu_String *string = cast(lulu_String *)lulu_Object_new(vm, LULU_TYPE_STRING, size_of_string(len));
    lulu_String_init(string, len, hash);
    memcpy(string->data, data, len);
    return lulu_Table_intern_string(vm, &vm->strings, string);
}

void
lulu_String_free(lulu_VM *vm, lulu_String *self)
{
    lulu_Memory_free(vm, self, size_of_string(self->len));
}

// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
#define FNV1A_PRIME_32  16777619
#define FNV1A_OFFSET_32 2166136261

u32
lulu_String_hash(const char *data, isize len)
{
    u32 hash = FNV1A_OFFSET_32;
    for (isize i = 0; i < len; i++) {
        hash ^= cast(byte)data[i];
        hash *= FNV1A_PRIME_32;
    }
    return hash;
}
