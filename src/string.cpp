#include <stdio.h>  // sprintf
#include <limits.h> // INT_WIDTH
#include <float.h>  // DBL_DECIMAL_GI

#include "string.hpp"
#include "vm.hpp"

void
builder_init(Builder *b)
{
    dynamic_init(&b->buffer);
}

size_t
builder_len(const Builder *b)
{
    return len(b->buffer);
}

size_t
builder_cap(const Builder *b)
{
    return cap(b->buffer);
}

void
builder_reset(Builder *b)
{
    dynamic_reset(&b->buffer);
}

void
builder_destroy(lulu_VM *vm, Builder *b)
{
    dynamic_delete(vm, &b->buffer);
}

void
builder_write_string(lulu_VM *vm, Builder *b, LString s)
{
    // Nothing to do?
    if (len(s) == 0) {
        return;
    }

    size_t old_len = builder_len(b);
    size_t new_len = old_len + len(s) + 1; // Include nul char for allocation.

    dynamic_resize(vm, &b->buffer, new_len);

    // Append the new data. For our purposes we assume that `b->buffer.data`
    // and `s.data` never alias. This is not a generic function.
    memcpy(&b->buffer[old_len], raw_data(s), len(s));
    b->buffer[new_len - 1] = '\0';
    dynamic_pop(&b->buffer); // Don't include nul char when calling `len()`.
}

void
builder_write_char(lulu_VM *vm, Builder *b, char ch)
{
    LString s{&ch, 1};
    builder_write_string(vm, b, s);
}

void
builder_write_int(lulu_VM *vm, Builder *b, int i)
{
    char buf[INT_WIDTH * 2];
    int  written = sprintf(buf, "%i", i);
    builder_write_string(vm, b, Slice(buf, cast_size(written)));
}

void
builder_write_number(lulu_VM *vm, Builder *b, Number n)
{
    char buf[DBL_DECIMAL_DIG * 2];
    int  written = sprintf(buf, LULU_NUMBER_FMT, n);
    builder_write_string(vm, b, Slice(buf, cast_size(written)));
}

void
builder_write_pointer(lulu_VM *vm, Builder *b, void *p)
{
    char buf[sizeof(p) * CHAR_BIT];
    int  written = sprintf(buf, "%p", p);
    builder_write_string(vm, b, Slice(buf, cast_size(written)));
}

LString
builder_to_string(const Builder *b)
{
    LString s = LString(b->buffer);
    // Ensure the `builder_write_*` family worked properly.
    lulu_assert(len(s) == 0 || raw_data(s)[len(s)] == '\0');
    return s;
}

const char *
builder_to_cstring(const Builder *b)
{
    LString s = builder_to_string(b);
    return raw_data(s);
}

u32
hash_string(LString text)
{
    static constexpr u32
    FNV1A_OFFSET = 0x811c9dc5,
    FNV1A_PRIME  = 0x01000193;

    u32 hash = FNV1A_OFFSET;
    for (char c : text) {
        hash ^= u32(c);
        hash *= FNV1A_PRIME;
    }

    return hash;
}

void
intern_init(Intern *t)
{
    t->table.data  = nullptr;
    t->table.len   = 0;
    t->count       = 0;
}

static size_t
intern_cap(const Intern *t)
{
    return len(t->table);
}

// Assumes `cap` is always a power of 2.
static size_t
intern_clamp_index(u32 hash, size_t cap)
{
    return cast_size(hash) & (cap - 1);
}

void
intern_resize(lulu_VM *vm, Intern *t, size_t new_cap)
{
    Slice<Object *> new_table = slice_make<Object *>(vm, new_cap);
    // Zero out the new memory
    fill(new_table, cast(Object *)nullptr);

    // Rehash all strings from the old table to the new table.
    for (Object *list : t->table) {
        Object *node = list;
        // Rehash all children for this list.
        while (node != nullptr) {
            OString *s = &node->ostring;
            size_t   i = intern_clamp_index(s->hash, new_cap);

            // Save because it's about to be replaced.
            Object *next  = s->next;

            // Chain this node in the new table, using the new main index.
            s->next      = new_table[i];
            new_table[i] = node;
            node         = next;
        }
    }
    slice_delete(vm, t->table);
    t->table = new_table;
}

void
intern_destroy(lulu_VM *vm, Intern *t)
{
    for (Object *list : t->table) {
        Object *node = list;
        while (node != nullptr) {
            Object *next = node->base.next;
            object_free(vm, node);
            node = next;
        }
    }
    mem_delete(vm, raw_data(t->table), len(t->table));
    intern_init(t);
}

OString *
ostring_new(lulu_VM *vm, LString text)
{
    Intern  *t    = &vm->intern;
    u32      hash = hash_string(text);
    size_t   i    = intern_clamp_index(hash, intern_cap(t));
    for (Object *node = t->table[i]; node != nullptr; node = node->base.next) {
        OString *s = &node->ostring;
        if (s->hash == hash) {
            if (text == LString(s->data, s->len)) {
                return s;
            }
        }
    }

    // We assume that `len(t->table)` is never 0 by this point.
    // No need to add 1 to len; `data[1]` is already embedded in the struct.
    OString *s = object_new<OString>(vm, &t->table[i], VALUE_STRING, len(text));
    s->hash = hash;
    s->len  = len(text);
    memcpy(s->data, raw_data(text), len(text));
    s->data[s->len] = 0;

    if (t->count + 1 > intern_cap(t)*3 / 4) {
        size_t new_cap = mem_next_size(t->count + 1);
        intern_resize(vm, t, new_cap);
    }
    t->count++;
    return s;
}
