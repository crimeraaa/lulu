#include "string.hpp"

void
builder_init(Builder &b)
{
    dynamic_init(b.buffer);
}

size_t
builder_len(const Builder &b)
{
    return len(b.buffer);
}

size_t
builder_cap(const Builder &b)
{
    return cap(b.buffer);
}

void
builder_reset(Builder &b)
{
    dynamic_reset(b.buffer);
}

void
builder_destroy(lulu_VM &vm, Builder &b)
{
    dynamic_delete(vm, b.buffer);
}

void
builder_write_char(lulu_VM &vm, Builder &b, char ch)
{
    String s{&ch, 1};
    builder_write_string(vm, b, s);
}

void
builder_write_string(lulu_VM &vm, Builder &b, String s)
{
    // Nothing to do?
    if (len(s) == 0) {
        return;
    }

    size_t old_len = builder_len(b);
    size_t new_len = old_len + len(s) + 1; // Include nul char for allocation.

    dynamic_resize(vm, b.buffer, new_len);

    // Append the new data. For our purposes we assume that `b.buffer.data`
    // and `s.data` never alias. This is not a generic function.
    memcpy(&b.buffer[old_len], raw_data(s), len(s));
    b.buffer[new_len - 1] = '\0';
    dynamic_pop(b.buffer); // Don't include nul char when calling `len()`.
}

String
builder_to_string(const Builder &b)
{
    // Ensure the `builder_write_*` family worked properly.
    lulu_assert(len(b.buffer) == 0 || raw_data(b.buffer)[len(b.buffer)] == '\0');
    return string_make(b.buffer);
}
