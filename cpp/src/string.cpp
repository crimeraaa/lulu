#include <limits.h> // INT_WIDTH
#include <stdio.h>  // sprintf
#include <stdlib.h> // strtod, strtoul

#include "lexer.hpp"
#include "vm.hpp"

static int
get_base(LString s)
{
    // Have a leading `'0'`?
    if (len(s) > 2 && s[0] == '0') {
        switch (s[1]) {
        case 'b':
        case 'B':
            return 2;

        case 'o':
        case 'O':
            return 8;

        case 'd':
        case 'D':
            return 10;

        case 'x':
        case 'X':
            return 16;

        default:
            break;
        }
    }
    return 0;
}

bool
lstring_to_number(LString s, Number *n, int base)
{
    // Need to detect the base prefix?
    if (base == 0) {
        base = get_base(s);
        // Skip the `0[bBoOdDxX]` prefix because `strto*` doesn't support `0b`.
        if (base != 0) {
            // s = s[2:]
            s = slice_from(s, 2);
        }
    }

    char *last;
    // Parsing a prefixed integer?
    if (base != 0) {
        // Got a base prefix with no content? e.g. `0b` or `0x`
        if (len(s) == 0) {
            return false;
        }
        unsigned long ul = strtoul(raw_data(s), &last, base);
        // @todo(2025-08-22) Account for overflow?
        *n = static_cast<Number>(ul);
    } else {
        *n = strtod(raw_data(s), &last);
    }

    // Skip trailing whitespace.
    while (*last == ' ' || *last == '\t' || *last == '\r' || *last == '\n') {
        last++;
    }

    // Success only when all characters in the string were succesfully
    // parsed as part of the number.
    return last == end(s);
}

LString
number_to_lstring(Number n, Slice<char> buf)
{
    isize written = sprintf(raw_data(buf), LULU_NUMBER_FMT, n);
    lulu_assert(1 <= written && written < len(buf));
    LString ls{raw_data(buf), written};
    return ls;
}

void
builder_reset(Builder *b)
{
    dynamic_reset(&b->buffer);
}

void
builder_destroy(lulu_VM *vm, Builder *b)
{
    dynamic_delete(vm, b->buffer);
}

void
builder_write_char(lulu_VM *vm, Builder *b, char ch)
{
    dynamic_push(vm, &b->buffer, ch);
}

void
builder_write_lstring(lulu_VM *vm, Builder *b, LString s)
{
    // Nothing to do?
    if (len(s) == 0) {
        return;
    }

    isize old_len = builder_len(*b);
    isize new_len = old_len + len(s); // Does not include nul char.

    dynamic_resize(vm, &b->buffer, new_len);

    // Append the new data. For our purposes we assume that `b->buffer.data`
    // and `s.data` never alias. This is not a generic function.
    memcpy(&b->buffer[old_len], raw_data(s), static_cast<usize>(len(s)));
}


/**
 * @note(2025-07-20)
 *      `Fmt` must be an existing entity, e.g.
 *      `static const char fmt[] = "%i";` rather than a string literal
 *      because of course C++ templates don't let you do it.
 */
template<auto Bufsize, const char *Fmt, class Arg>
static void
builder_write(lulu_VM *vm, Builder *b, Arg arg)
{
    Array<char, Bufsize> buf;

    isize written = sprintf(raw_data(buf), Fmt, arg);
    lulu_assert(1 <= written && written < len(buf));

    LString ls{raw_data(buf), written};
    builder_write_lstring(vm, b, ls);
}

void
builder_write_int(lulu_VM *vm, Builder *b, int i)
{
    static constexpr const char fmt[] = "%i";
    builder_write<INT_WIDTH * 2, fmt>(vm, b, i);
}

void
builder_write_number(lulu_VM *vm, Builder *b, Number n)
{
    static constexpr const char fmt[] = LULU_NUMBER_FMT;
    builder_write<LULU_NUMBER_BUFSIZE, fmt>(vm, b, n);
}

void
builder_write_pointer(lulu_VM *vm, Builder *b, void *p)
{
    static constexpr const char fmt[] = "%p";
    builder_write<sizeof(p) * CHAR_BIT, fmt>(vm, b, p);
}

void
builder_pop(Builder *b)
{
    dynamic_pop(&b->buffer);
}

LString
builder_to_string(const Builder &b)
{
    return lstring_from_slice(b.buffer);
}

const char *
builder_to_cstring(lulu_VM *vm, Builder *b)
{
    // Make no assumptions if buffer is already nul-terminated.
    dynamic_push(vm, &b->buffer, '\0');
    dynamic_pop(&b->buffer);
    return raw_data(b->buffer);
}

u32
hash_string(LString text)
{
    u32 hash = FNV1A_OFFSET;
    for (char c : text) {
        hash ^= static_cast<u32>(c);
        hash *= FNV1A_PRIME;
    }
    return hash;
}

// Assumes `cap` is always a power of 2.
// The result must be unsigned in order to have sane bitwise operations.
static usize
intern_clamp_index(u32 hash, isize cap)
{
    return static_cast<usize>(hash) & static_cast<usize>(cap - 1);
}

void
intern_resize(lulu_VM *vm, Intern *t, isize new_cap)
{
    Slice<Object *> new_table = slice_make<Object *>(vm, new_cap);
    // Zero out the new memory
    fill(new_table, static_cast<Object *>(nullptr));

    // Rehash all strings from the old table to the new table.
    for (Object *list : t->table) {
        Object *node = list;
        // Rehash all children for this list.
        while (node != nullptr) {
            OString *s = &node->ostring;
            usize    i = intern_clamp_index(s->hash, new_cap);

            // Save because it's about to be replaced.
            Object *next = s->next;

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
            Object *next = node->next();
            object_free(vm, node);
            node = next;
        }
    }
    slice_delete(vm, t->table);
}

OString *
ostring_new(lulu_VM *vm, LString text)
{
    Intern *t    = &G(vm)->intern;
    u32     hash = hash_string(text);
    usize   i    = intern_clamp_index(hash, len(t->table));
    for (Object *node = t->table[i]; node != nullptr; node = node->next()) {
        OString *s = &node->ostring;
        if (s->hash == hash) {
            if (slice_eq(text, s->to_lstring())) {
                return s;
            }
        }
    }

    // We assume that `len(t->table)` is never 0 by this point.
    // No need to add 1 to len; `data[1]` is already embedded in the struct.
    OString *s = object_new<OString>(vm, &t->table[i], VALUE_STRING, len(text));
    s->len     = len(text);
    s->hash    = hash;
    s->keyword_type = TOKEN_INVALID;
    s->data[s->len] = 0;
    memcpy(s->data, raw_data(text), static_cast<usize>(len(text)));

#ifdef LULU_DEBUG_LOG_GC
    object_gc_print(s->to_object(), "[NEW] string");
#endif // LULU_DEBUG_LOG_GC

    isize n = len(t->table);
    lulu_assume(n > 0);

    // Count refers to total number of linked list nodes, not occupied array
    // slots. We probably want to rehash anyway to reduce clustering.
    if (t->count + 1 > n) {
        vm_push_value(vm, Value::make_string(s));
        // We assume `n` is a power of 2.
        intern_resize(vm, t, n << 1);
        vm_pop_value(vm);
    }
    t->count++;
    return s;
}
