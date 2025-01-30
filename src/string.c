/// local
#include "string.h"
#include "table.h"
#include "vm.h"

/// standard
#include <string.h> // memcpy, memset

#define size_of_string(len)     (size_of(OString) + size_of(char) * ((len) + 1))

void
ostring_init(OString *self, isize len, u32 hash)
{
    self->len       = len;
    self->hash      = hash;
    self->data[len] = '\0';
}

OString *
ostring_new(lulu_VM *vm, const char *data, isize len)
{
    const u32 hash     = ostring_hash(data, len);
    OString  *interned = table_find_string(&vm->strings, data, len, hash);
    if (interned) {
        return interned;
    }
    OString *string = cast(OString *)object_new(vm, LULU_TYPE_STRING, size_of_string(len));
    ostring_init(string, len, hash);
    memcpy(string->data, data, cast(usize)len);
    return table_intern_string(vm, &vm->strings, string);
}

void
ostring_free(lulu_VM *vm, OString *self)
{
    mem_free(vm, self, size_of_string(self->len));
}

// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
#define FNV1A_PRIME_32  16777619
#define FNV1A_OFFSET_32 2166136261

u32
ostring_hash(const char *data, isize len)
{
    u32 hash = FNV1A_OFFSET_32;
    for (isize i = 0; i < len; i++) {
        hash ^= cast(byte)data[i];
        hash *= FNV1A_PRIME_32;
    }
    return hash;
}
