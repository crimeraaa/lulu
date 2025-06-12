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
write_char(lulu_VM &vm, Builder &b, char ch)
{
    char   buf[] = {ch, 0};
    String s{buf, 1};
    write_string(vm, b, s);
}

void
write_string(lulu_VM &vm, Builder &b, String s)
{
    // Nothing to do?
    if (len(s) == 0) {
        return;
    }

    size_t old_len = builder_len(b);

    // Account for nul char for C compatibility.
    size_t new_len = old_len + len(s) + 1;

    dynamic_resize(vm, b.buffer, new_len);

    // Append the new data. For our purposes we assume that `b.buffer.data`
    // and `s.data` never alias. This is not a generic function.
    memcpy(&b.buffer[old_len], raw_data(s), len(s));
    dynamic_push(vm, b.buffer, '\0');

    // Don't include nul char in the final count.
    dynamic_pop(b.buffer);
}
