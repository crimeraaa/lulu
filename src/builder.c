/// local
#include "builder.h"
#include "memory.h"

/// standard
#include <string.h>

void
builder_init(lulu_VM *vm, Builder *self)
{
    self->vm     = vm;
    self->buffer = NULL;
    self->len    = 0;
    self->cap    = 0;
}

void
builder_reserve(Builder *self, isize new_cap)
{
    isize old_cap = self->cap;
    if (new_cap <= old_cap) {
        return;
    }

    self->buffer = array_resize(char, self->vm, self->buffer, old_cap, new_cap);
    self->cap    = new_cap;
}

void
builder_free(Builder *self)
{
    array_free(char, self->vm, self->buffer, self->cap);
}

void
builder_reset(Builder *self)
{
    self->len = 0;
}

void
builder_write_char(Builder *self, char ch)
{
    if (self->len >= self->cap) {
        builder_reserve(self, mem_grow_capacity(self->len));
    }
    self->buffer[self->len++] = ch;
}

void
builder_write_string(Builder *self, const char *data, isize len)
{
    isize old_len = self->len;
    isize new_len = old_len + len;
    if (new_len > self->cap) {
        builder_reserve(self, mem_grow_capacity(new_len));
    }
    // Address of the first element in `buffer` that we want to append to.
    char *start = self->buffer + old_len;
    memcpy(start, data, sizeof(data[0]) * cast(usize)len);
    self->len = new_len;
}

void
builder_write_cstring(Builder *self, cstring cstr)
{
    builder_write_string(self, cstr, cast(isize)strlen(cstr));
}

cstring
builder_to_string(Builder *self, isize *out_len)
{
    // Ensure nul termination.
    builder_write_char(self, '\0');
    self->len--;
    if (out_len) {
        *out_len = self->len;
    }
    return self->buffer;
}
