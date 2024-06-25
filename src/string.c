#include "string.h"
#include "table.h"
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
static uint32_t hash_string(View sv)
{
    uint32_t hash = FNV1A_OFFSET32;
    char     prev = 0;
    for (const char *ptr = sv.begin; ptr < sv.end; ptr++) {
        char ch = *ptr;
        if (ch == '\\' && prev != '\\') {
            prev = ch;
            continue;
        }
        hash ^= cast_byte((prev == '\\') ? get_escape(ch) : ch);
        hash *= FNV1A_PRIME32;
        if (prev == '\\')
            prev = 0;
    }
    return hash;
}

uint32_t luluStr_hash_raw(View sv)
{
    uint32_t hash = FNV1A_OFFSET32;
    for (const char *ptr = sv.begin; ptr < sv.end; ptr++) {
        hash ^= cast_byte(*ptr);
        hash *= FNV1A_PRIME32;
    }
    return hash;
}

String *luluStr_new(lulu_VM *vm, size_t len)
{
    // Note how we add 1 for the nul char.
    String *s = cast(String*, luluObj_new(vm, luluStr_size(len + 1), TYPE_STRING));
    s->len    = len;
    return s;
}

// Note we add 1 to `oldsz` because we previously allocated 1 extra by for nul.
void luluStr_free(lulu_VM *vm, String *s)
{
    luluMem_free_pointer(vm, luluObj_unlink(vm, &s->object), luluStr_size(s->len + 1));
}

static void build_string(String *s, View sv)
{
    char *end   = s->data; // For loop counter may skip.
    int   skips = 0;       // Number escape characters emitted.
    char  prev  = 0;

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
    s->len = view_len(sv) - skips;
}

static void end_string(String *s, uint32_t hash)
{
    s->data[s->len] = '\0';
    s->hash         = hash;
}

static String *copy_string(lulu_VM *vm, View sv, bool israw)
{
    uint32_t hash  = (israw) ? luluStr_hash_raw(sv) : hash_string(sv);
    String  *found = luluStr_find_interned(vm, sv, hash);
    size_t   len   = view_len(sv);
    if (found != NULL)
        return found;

    String *s = luluStr_new(vm, len);
    if (israw)
        memcpy(s->data, sv.begin, len);
    else
        build_string(s, sv);
    end_string(s, hash);

    // If we have escapes, are we really REALLY sure this isn't found?
    if (s->len != len) {
        found = luluStr_find_interned(vm, view_from_len(s->data, s->len), hash);
        if (found != NULL) {
            luluStr_free(vm, s);
            return found;
        }
    }
    luluStr_set_interned(vm, s);
    return s;
}

String *luluStr_copy_raw(lulu_VM *vm, View sv)
{
    return copy_string(vm, sv, true);
}

String *luluStr_copy(lulu_VM *vm, View sv)
{
    return copy_string(vm, sv, false);
}

String *luluStr_concat(lulu_VM *vm, int argc, const Value argv[], size_t len)
{
    String *s      = luluStr_new(vm, len);
    View    sv     = view_from_len(s->data, s->len);
    size_t  offset = 0;

    // We already built each individual string so no need to interpret escapes.
    for (int i = 0; i < argc; i++) {
        const String *arg = as_string(&argv[i]);
        memcpy(&s->data[offset], arg->data, arg->len);
        offset += arg->len;
    }
    end_string(s, hash_string(sv));
    String *found = luluStr_find_interned(vm, sv, s->hash);
    if (found != NULL) {
        luluStr_free(vm, s);
        return found;
    }
    luluStr_set_interned(vm, s);
    return s;
}

void luluStr_set_interned(lulu_VM *vm, const String *s)
{
    Table *t = &vm->strings;
    Value  k = make_string(s);
    luluTbl_set(vm, t, &k, &k);
}

String *luluStr_find_interned(lulu_VM *vm, View sv, uint32_t hash)
{
    Table *t = &vm->strings;
    if (t->count == 0)
        return NULL;

    uint32_t i = hash % t->cap;
    size_t   n = view_len(sv);
    for (;;) {
        Entry *ent = &t->entries[i];
        // The strings table only ever has completely empty or full entries.
        if (is_nil(&ent->key) && is_nil(&ent->value))
            return NULL;

        // We assume ALL valid (i.e: non-nil) keys are strings.
        String *s = as_string(&ent->key);
        if (s->len == n && s->hash == hash) {
            if (cstr_eq(s->data, sv.begin, n))
                return s;
        }
        i = (i + 1) % t->cap;
    }
}
