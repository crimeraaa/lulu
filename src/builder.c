/// local
#include "builder.h"
#include "memory.h"

/// standard
#include <string.h>

void
lulu_Builder_init(lulu_VM *vm, lulu_Builder *self)
{
    self->vm     = vm;
    self->buffer = NULL;
    self->len    = 0;
    self->cap    = 0;
}

void
lulu_Builder_reserve(lulu_Builder *self, isize new_cap)
{
    isize old_cap = self->cap;
    if (new_cap <= old_cap) {
        return;
    }

    self->buffer = rawarray_resize(char, self->vm, self->buffer, old_cap, new_cap);
    self->cap    = new_cap;
}

void
lulu_Builder_free(lulu_Builder *self)
{
    rawarray_free(char, self->vm, self->buffer, self->cap);
}

void
lulu_Builder_reset(lulu_Builder *self)
{
    self->len = 0;
}

void
lulu_Builder_write_char(lulu_Builder *self, char ch)
{
    if (self->len >= self->cap) {
        lulu_Builder_reserve(self, lulu_Memory_grow_capacity(self->cap));
    }
    self->buffer[self->len++] = ch;
}

void
lulu_Builder_write_string(lulu_Builder *self, const char *data, isize len)
{
    isize old_len = self->len;
    isize new_len = old_len + len;
    if (new_len > self->cap) {
        // Next power of 2
        isize new_cap = 1;
        while (new_cap < new_len) {
            new_cap *= 2;
        }
        lulu_Builder_reserve(self, new_cap);
    }
    for (isize i = 0; i < len; i++) {
        self->buffer[old_len + i] = data[i];
    }
    self->len = new_len;
}

void
lulu_Builder_write_cstring(lulu_Builder *self, cstring cstr)
{
    lulu_Builder_write_string(self, cstr, cast(isize)strlen(cstr));
}

const char *
lulu_Builder_to_string(lulu_Builder *self, isize *out_len)
{
    *out_len = self->len;
    return self->buffer;
}
