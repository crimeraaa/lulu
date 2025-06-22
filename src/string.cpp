#include "string.hpp"
#include "vm.hpp"

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

static u32
hash_string(String text)
{
    static constexpr u32
    FNV1A_OFFSET = 0x811c9dc5,
    FNV1A_PRIME  = 0x01000193;

    u32 hash = FNV1A_OFFSET;
    for (char c : text) {
        hash ^= cast(u32, c);
        hash *= FNV1A_PRIME;
    }

    return hash;
}

void
intern_init(Intern &t)
{
    t.table.data  = nullptr;
    t.table.len   = 0;
    t.count       = 0;
}

void
intern_resize(lulu_VM &vm, Intern &t, size_t new_cap)
{
    Slice<OString *> new_table{mem_make<OString *>(vm, new_cap), new_cap};
    // Zero out the new memory
    for (auto &s : new_table) {
        s = nullptr;
    }

    // Rehash all strings from the old table to the new table.
    for (OString *list : t.table) {
        OString *node = list;
        // Rehash all children for this list.
        while (node != nullptr) {
            size_t   i    = cast_size(node->hash) % new_cap;
            OString *next = cast(OString *, node->base.next);

            // Chain this node in the new table, using the new main index.
            node->base.next = cast(Object *, new_table[i]);
            new_table[i]    = node;

            node = next;
        }
    }
    mem_delete(vm, raw_data(t.table), len(t.table));
    t.table = new_table;
}

void
intern_destroy(lulu_VM &vm, Intern &t)
{
    for (OString *list : t.table) {
        OString *node = list;
        while (node != nullptr) {
            OString *next = cast(OString *, node->base.next);
            ostring_free(vm, node);
            node = next;
        }
    }
    mem_delete(vm, raw_data(t.table), len(t.table));
    intern_init(t);
}

OString *
ostring_new(lulu_VM &vm, String text)
{
    Intern  &t    = vm.intern;
    u32      hash = hash_string(text);
    size_t   i    = cast_size(hash) % len(t.table);
    for (OString *node = t.table[i];
        node != nullptr;
        node = cast(OString *, node->base.next)) {
        if (node->hash == hash && node->len == len(text)) {
            String tmp{node->data, node->len};
            if (string_eq(text, tmp)) {
                return node;
            }
        }
    }

    // We assume that `len(t.table)` is never 0 by this point.
    Object **list = cast(Object **, &t.table[i]);

    // No need to add 1 to len; `data[1]` is already embedded in the struct.
    OString *s = object_new<OString>(vm, list, LULU_TYPE_STRING, len(text));
    s->hash = hash;
    s->len  = len(text);
    memcpy(s->data, raw_data(text), len(text));
    s->data[s->len] = 0;

    if (t.count + 1 > len(t.table)*3 / 4) {
        size_t new_cap = mem_next_size(t.count + 1);
        intern_resize(vm, t, new_cap);
    }
    t.count++;
    return s;
}

void
ostring_free(lulu_VM &vm, OString *o)
{
    mem_free(vm, o, o->len);
}
