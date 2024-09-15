#include "string.h"
#include "table.h"
#include "vm.h"

#include <string.h>

#define size_of_string(len)     (size_of(lulu_String) + size_of(char) * (len + 1))
#define alloc_string(vm, len)   cast(lulu_String *)lulu_Object_new(vm, LULU_TYPE_STRING, size_of_string(len))

void
lulu_String_init(lulu_String *self, isize len, u32 hash)
{
    self->len       = len;
    self->hash      = hash;
    self->data[len] = '\0';
}

lulu_String *
lulu_String_new(lulu_VM *vm, String src)
{
    const u32    hash     = lulu_String_hash(src);
    lulu_String *interned = lulu_Table_find_string(&vm->strings, src, hash);
    if (interned) {
        return interned;
    }
    lulu_String *string = alloc_string(vm, src.len);
    lulu_String_init(string, src.len, hash);
    memcpy(string->data, src.data, src.len);
    return lulu_Table_intern_string(vm, &vm->strings, string);
}

void
lulu_String_free(lulu_VM *vm, lulu_String *self)
{
    lulu_Memory_free(vm, self, size_of_string(self->len));
}

lulu_String *
lulu_String_concat(lulu_VM *vm, lulu_String *a, lulu_String *b)
{
    isize        len    = a->len + b->len;
    lulu_String *string = alloc_string(vm, len);
    lulu_String_init(string, len, 0);
    memcpy(string->data,          a->data, a->len);
    memcpy(&string->data[a->len], b->data, b->len);

    String tmp   = {string->data, string->len};
    string->hash = lulu_String_hash(tmp);
    
    lulu_String *interned = lulu_Table_find_string(&vm->strings, tmp, string->hash);
    if (interned) {
        vm->objects = string->base.next;
        lulu_String_free(vm, string);
        return interned;
    }
    return lulu_Table_intern_string(vm, &vm->strings, string);
}


// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
#define FNV1A_PRIME_32  16777619
#define FNV1A_OFFSET_32 2166136261

u32
lulu_String_hash(String string)
{
    u32 hash = FNV1A_OFFSET_32;
    for (isize i = 0; i < string.len; i++) {
        hash ^= cast(byte)string.data[i];
        hash *= FNV1A_PRIME_32;
    }
    return hash;
}
void
String_Builder_init(lulu_VM *vm, String_Builder *self)
{
    self->vm     = vm;
    self->buffer = NULL;
    self->len    = 0;
    self->cap    = 0;
}

void
String_Builder_reserve(String_Builder *self, isize new_cap)
{
    isize old_cap = self->cap;
    if (new_cap <= old_cap) {
        return;
    }
    
    self->buffer = rawarray_resize(char, self->vm, self->buffer, old_cap, new_cap);
    self->cap    = new_cap;
}

void
String_Builder_free(String_Builder *self)
{
    rawarray_free(char, self->vm, self->buffer, self->cap);
}

void
String_Builder_write_char(String_Builder *self, char ch)
{
    if (self->len >= self->cap) {
        String_Builder_reserve(self, GROW_CAPACITY(self->cap));
    }
    self->buffer[self->len++] = ch;
}

void
String_Builder_write_string(String_Builder *self, String str)
{
    isize old_len = self->len;
    isize new_len = old_len + str.len;
    if (new_len > self->cap) {
        // Next power of 2
        isize new_cap = 1;
        while (new_cap < new_len) {
            new_cap *= 2;
        }
        String_Builder_reserve(self, new_cap);
    }
    for (isize i = 0; i < str.len; i++) {
        self->buffer[old_len + i] = str.data[i];
    }
    self->len = new_len;
}

void
String_Builder_write_cstring(String_Builder *self, cstring cstr)
{
    String str = {cstr, cast(isize)strlen(cstr)};
    String_Builder_write_string(self, str);
}
