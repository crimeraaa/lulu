#include "string.h"
#include "table.h"
#include "vm.h"

// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
#define FNV1A_PRIME32   0x01000193
#define FNV1A_OFFSET32  0x811c9dc5
#define FNV1A_PRIME64   0x00000100000001B3
#define FNV1A_OFFSET64  0xcbf29ce484222325

uint32_t luluStr_hash(View sv)
{
    uint32_t hash = FNV1A_OFFSET32;
    for (const char *ptr = sv.begin; ptr < sv.end; ptr++) {
        hash ^= cast_byte(*ptr);
        hash *= FNV1A_PRIME32;
    }
    return hash;
}

String *luluStr_new(lulu_VM *vm, const char *cs, size_t len, uint32_t hash)
{
    // Note how we add 1 for the nul char.
    String *s = cast(String*, luluObj_new(vm, luluStr_size(len + 1), TYPE_STRING));
    s->len    = len;
    s->hash   = hash;
    if (cs != NULL)
        memcpy(s->data, cs, len);
    s->data[len] = '\0';
    return s;
}

// Note we add 1 to `oldsz` because we previously allocated 1 extra by for nul.
void luluStr_free(lulu_VM *vm, String *s)
{
    luluMem_free_pointer(vm, luluObj_unlink(vm, &s->object), luluStr_size(s->len + 1));
}

static String *find_if_interned(lulu_VM *vm, String *s)
{
    View    view  = view_from_len(s->data, s->len);
    String *found = luluStr_find_interned(vm, view, s->hash);
    if (found != NULL) {
        luluStr_free(vm, s);
        return found;
    }
    luluStr_set_interned(vm, s);
    return s;
}

String *luluStr_copy(lulu_VM *vm, View sv)
{
    uint32_t hash  = luluStr_hash(sv);
    String  *found = luluStr_find_interned(vm, sv, hash);
    size_t   len   = view_len(sv);
    if (found != NULL)
        return found;

    String *s = luluStr_new(vm, sv.begin, len, hash);
    // If we have escapes, are we really REALLY sure this isn't found?
    if (s->len != len)
        return find_if_interned(vm, s);
    luluStr_set_interned(vm, s);
    return s;
}

String *luluStr_concat(lulu_VM *vm, int argc, const Value argv[], size_t len)
{
    String *s      = luluStr_new(vm, NULL, len, 0);
    View    v      = view_from_len(s->data, s->len);
    size_t  offset = 0;

    // We already built each individual string so no need to interpret escapes.
    for (int i = 0; i < argc; i++) {
        const String *arg = as_string(&argv[i]);
        memcpy(&s->data[offset], arg->data, arg->len);
        offset += arg->len;
    }
    s->hash = luluStr_hash(v);
    return find_if_interned(vm, s);
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
