#include "string.h"
#include "vm.h"

// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
#define FNV1A_PRIME32   0x01000193
#define FNV1A_OFFSET32  0x811c9dc5
#define FNV1A_PRIME64   0x00000100000001B3
#define FNV1A_OFFSET64  0xcbf29ce484222325

static char get_escape(char ch)
{
    switch (ch) {
    case '\\':  return '\\';
    case '\'':  return '\'';
    case '\"':  return '\"';

    case '0':   return '\0';
    case 'a':   return '\a';
    case 'b':   return '\b';
    case 'f':   return '\f';
    case 'n':   return '\n';
    case 'r':   return '\r';
    case 't':   return '\t';
    case 'v':   return '\v';

    default:    return ch;   // TODO: Warn user? Throw error?
    }
}

// Note that we need to hash escapes correctly too.
static uint32_t hash_string(StringView sv)
{
    uint32_t hash = FNV1A_OFFSET32;
    char     prev = 0;
    for (const char *ptr = sv.begin; ptr < sv.end; ptr++) {
        char ch = *ptr;
        if (ch == '\\' && prev != '\\') {
            prev = ch;
            continue;
        }
        hash ^= cast(Byte, (prev == '\\') ? get_escape(ch) : ch);
        hash *= FNV1A_PRIME32;
        if (prev == '\\') {
            prev = 0;
        }
    }
    return hash;
}

uint32_t hash_rstring(StringView sv)
{
    uint32_t hash = FNV1A_OFFSET32;
    for (const char *ptr = sv.begin; ptr < sv.end; ptr++) {
        hash ^= cast(Byte, *ptr);
        hash *= FNV1A_PRIME32;
    }
    return hash;
}

String *new_string(int len, Alloc *al)
{
    // Note how we add 1 for the nul char.
    String *s = cast(String*, new_object(string_size(len + 1), TYPE_STRING, al));
    s->len    = len;
    return s;
}

// Note we add 1 to `oldsz` because we previously allocated 1 extra by for nul.
void free_string(String *s, Alloc *al)
{
    free_pointer(s, string_size(s->len + 1), al);
}

static void build_string(String *s, StringView sv)
{
    char   *end   = s->data; // For loop counter may skip.
    int     skips = 0;          // Number escape characters emitted.
    char    prev  = 0;

    for (const char *ptr = sv.begin; ptr < sv.end; ptr++) {
        char ch = *ptr;
        // Handle `"\\"` appropriately.
        if (ch == '\\' && prev != '\\') {
            skips++;
            prev = ch;
            continue;
        }
        // TODO: 3-digit number literals (0-prefixed), 2-digit ASCII codes
        if (prev == '\\') {
            *end = get_escape(ch);
            prev = 0;
        } else {
            *end = ch;
        }
        end++;
    }
    *end   = '\0';
    s->len = sv.len - skips;
}

static void end_string(String *s, uint32_t hash)
{
    s->data[s->len] = '\0';
    s->hash         = hash;
}

static String *copy_string_or_rstring(VM *vm, StringView sv, bool israw)
{
    Alloc   *al    = &vm->allocator;
    uint32_t hash  = (israw) ? hash_rstring(sv) : hash_string(sv);
    String  *found = find_interned(vm, sv, hash);

    // Is this string already found?
    if (found != NULL) {
        return found;
    }

    String *s = new_string(sv.len, al);
    if (israw) {
        memcpy(s->data, sv.begin, sv.len);
    } else {
        build_string(s, sv);
    }
    end_string(s, hash);

    // If we have escapes, are we really REALLY sure this isn't found?
    if (s->len != sv.len) {
        found = find_interned(vm, sv_create_from_len(s->data, s->len), hash);
        if (found != NULL) {
            remove_object(&vm->objects, &s->object);
            free_string(s, al);
            return found;
        }
    }
    set_interned(vm, s);
    return s;
}

String *copy_rstring(lulu_VM *vm, StringView sv)
{
    return copy_string_or_rstring(vm, sv, true);
}

String *copy_string(lulu_VM *vm, StringView sv)
{
    return copy_string_or_rstring(vm, sv, false);
}

String *concat_strings(lulu_VM *vm, int argc, const Value argv[], int len)
{
    Alloc     *al     = &vm->allocator;
    String    *s      = new_string(len, al);
    StringView sv     = sv_create_from_len(s->data, s->len);
    int        offset = 0;

    // We already built each individual string so no need to interpret escapes.
    for (int i = 0; i < argc; i++) {
        const String *arg = as_string(&argv[i]);
        memcpy(s->data + offset, arg->data, arg->len);
        offset += arg->len;
    }
    end_string(s, hash_string(sv));
    String *found = find_interned(vm, sv, s->hash);
    if (found != NULL) {
        remove_object(&vm->objects, &s->object);
        free_string(s, al);
        return found;
    }
    set_interned(vm, s);
    return s;
}
