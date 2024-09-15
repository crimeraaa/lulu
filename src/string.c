#include "string.h"
#include "memory.h"

#include <string.h>

#define string_size_of(len)     (size_of(lulu_String) + size_of(char) * (len + 1))
#define string_alloc(vm, len)   cast(lulu_String *)lulu_Object_new(vm, LULU_TYPE_STRING, string_size_of(len))

static void
string_init(lulu_String *self, isize len)
{
    self->len       = len;
    self->data[len] = '\0';
}


lulu_String *
lulu_String_new(lulu_VM *vm, String src)
{
    lulu_String *string = string_alloc(vm, src.len);
    string_init(string, src.len);
    memcpy(string->data, src.data, src.len);
    return string;
}

void
lulu_String_free(lulu_VM *vm, lulu_String *self)
{
    lulu_Memory_free(vm, self, string_size_of(self->len));
}

lulu_String *
lulu_String_concat(lulu_VM *vm, lulu_String *a, lulu_String *b)
{
    isize        len    = a->len + b->len;
    lulu_String *string = string_alloc(vm, len);
    string_init(string, len);
    memcpy(string->data,          a->data, a->len);
    memcpy(&string->data[a->len], b->data, b->len);
    return string;
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
