#include "string.hpp"
#include "vm.hpp"

static const OString
TOMBSTONE{};

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
intern_init(Intern &i)
{
    i.table.data  = nullptr;
    i.table.len   = 0;
    i.list        = nullptr;
    i.count       = 0;
}

void
intern_destroy(lulu_VM &vm, Intern &t)
{
    mem_delete(vm, raw_data(t.table), len(t.table));
    OString *prev;
    for (OString *s = cast(OString *, t.list); s != nullptr; s = prev) {
        prev = cast(OString *, s->base.prev);
        ostring_free(vm, s);
    }
    intern_init(t);
}

static OString *
intern_find_string(Slice<OString *> t, String text, u32 hash)
{
    size_t cap = len(t);
    if (cap == 0) {
        return nullptr;
    }
    for (size_t i = cast(size_t, hash) % cap; /* empty */; i = (i + 1) % cap) {
        OString *o = t[i];
        if (o == nullptr) {
            break;
        } else if (o == &TOMBSTONE) {
            continue;
        }

        // Compare hashes first for speed.
        if (o->hash == hash && o->len == len(text)) {
            if (memcmp(raw_data(text), o->data, len(text)) == 0) {
                return o;
            }
        }
    }
    return nullptr;
}

struct Result {
    Intern_Entry *entry;
    bool          is_new;
};

static Result
intern_find_ostring(Slice<OString *> t, OString *s)
{
    Intern_Entry *tomb = nullptr;
    const size_t  cap = len(t);
    for (size_t i = cast(size_t, s->hash) % cap; /* empty */; i = (i + 1) % cap) {
        OString *s2 = t[i];
        // This chain cannot possibly contain `s`.
        if (s2 == nullptr) {
            const bool    is_new = (tomb == nullptr);
            Intern_Entry *e      = (is_new) ? &t[i] : tomb;
            return {e, is_new};
        } else if (s2 == &TOMBSTONE) {
            if (tomb == nullptr) {
                tomb = &t[i];
            }
            continue;
        } else if (s2 == s) {
            return {&t[i], true};
        }
    }
    lulu_unreachable();
}

static void
intern_resize(lulu_VM &vm, Intern &i, size_t new_cap)
{
    Slice<OString *> next{mem_make<OString *>(vm, new_cap), new_cap};
    // Zero out the new memory
    for (auto &s : next) {
        s = nullptr;
    }

    size_t n = 0;
    for (auto s : i.table) {
        if (s == nullptr) {
            continue;
        }
        Result r = intern_find_ostring(next, s);
        *r.entry = s;
        n++;
    }
    mem_delete(vm, raw_data(i.table), len(i.table));
    i.table = next;
    i.count = n;
}

OString *
ostring_new(lulu_VM &vm, String text)
{
    Intern  &t    = vm.intern;
    u32      hash = hash_string(text);
    OString *s    = intern_find_string(t.table, text, hash);
    if (s != nullptr) {
        return s;
    }

    // No need to add 1 to len; `data[1]` is already embedded in the struct.
    s = object_new<OString>(vm, &t.list, LULU_TYPE_STRING, len(text));
    s->hash = hash;
    s->len  = len(text);
    memcpy(s->data, raw_data(text), len(text));
    s->data[s->len] = 0;

    if (t.count + 1 > len(t.table)*3 / 4) {
        size_t new_cap = mem_next_size(t.count + 1);
        intern_resize(vm, t, new_cap);
    }

    Result r = intern_find_ostring(t.table, s);
    if (r.is_new) {
        t.count++;
    }
    *r.entry = s;
    return s;
}

void
ostring_free(lulu_VM &vm, OString *o)
{
    mem_free(vm, o, o->len);
}
