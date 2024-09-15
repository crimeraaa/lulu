#include "string.h"
#include "memory.h"

#include <string.h>

void
lulu_String_Builder_init(lulu_VM *vm, lulu_String_Builder *self)
{
    self->vm     = vm;
    self->buffer = NULL;
    self->len    = 0;
    self->cap    = 0;
}

void
lulu_String_Builder_reserve(lulu_String_Builder *self, isize new_cap)
{
    isize old_cap = self->cap;
    if (new_cap <= old_cap) {
        return;
    }
    
    self->buffer = rawarray_resize(char, self->vm, self->buffer, old_cap, new_cap);
    self->cap    = new_cap;
}

void
lulu_String_Builder_free(lulu_String_Builder *self)
{
    rawarray_free(char, self->vm, self->buffer, self->cap);
}

void
lulu_String_Builder_write_char(lulu_String_Builder *self, char ch)
{
    if (self->len >= self->cap) {
        lulu_String_Builder_reserve(self, GROW_CAPACITY(self->cap));
    }
    self->buffer[self->len++] = ch;
}

void
lulu_String_Builder_write_string(lulu_String_Builder *self, lulu_String_View str)
{
    isize old_len = self->len;
    isize new_len = old_len + str.len;
    if (new_len > self->cap) {
        // Next power of 2
        isize new_cap = 1;
        while (new_cap < new_len) {
            new_cap *= 2;
        }
        lulu_String_Builder_reserve(self, new_cap);
    }
    for (isize i = 0; i < str.len; i++) {
        self->buffer[old_len + i] = str.data[i];
    }
    self->len = new_len;
}

void
lulu_String_Builder_write_cstring(lulu_String_Builder *self, cstring cstr)
{
    lulu_String_View str = {cstr, cast(isize)strlen(cstr)};
    lulu_String_Builder_write_string(self, str);
}
