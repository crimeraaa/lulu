#include "string.h"
#include "table.h"
#include "vm.h"

// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
#define FNV1A_PRIME32   0x01000193
#define FNV1A_OFFSET32  0x811c9dc5
#define FNV1A_PRIME64   0x00000100000001B3
#define FNV1A_OFFSET64  0xcbf29ce484222325

uint32_t luluStr_hash(const char *cs, size_t len)
{
    uint32_t hash = FNV1A_OFFSET32;
    for (size_t i = 0; i < len; i++) {
        hash ^= cast_byte(cs[i]);
        hash *= FNV1A_PRIME32;
    }
    return hash;
}

String *luluStr_new(lulu_VM *vm, const char *cs, size_t len, uint32_t hash)
{
    // Note how we add 1 for the nul char.
    String *s = cast_string(luluObj_new(vm, luluStr_size(len + 1), TYPE_STRING));
    s->len    = len;
    s->hash   = hash;
    memcpy(s->data, cs, len);
    s->data[len] = '\0';
    return s;
}

// Note we add 1 to `oldsz` because we previously allocated 1 extra by for nul.
void luluStr_free(lulu_VM *vm, String *s)
{
    luluMem_free_pointer(vm, luluObj_unlink(vm, &s->object), luluStr_size(s->len + 1));
}

String *luluStr_copy(lulu_VM *vm, const char *cs, size_t len)
{
    uint32_t hash  = luluStr_hash(cs, len);
    String  *found = luluStr_find_interned(vm, cs, len, hash);
    if (found != nullptr)
        return found;

    String *s = luluStr_new(vm, cs, len, hash);
    luluStr_set_interned(vm, s);
    return s;
}

void luluStr_set_interned(lulu_VM *vm, const String *s)
{
    Table *t = &vm->strings;
    Value  k;
    setv_string(&k, s);
    luluTbl_set(vm, t, &k, &k);
}

String *luluStr_find_interned(lulu_VM *vm, const char *cs, size_t len, uint32_t hash)
{
    Table *t = &vm->strings;
    if (t->count == 0)
        return nullptr;

    uint32_t i = hash % t->cap;
    for (;;) {
        Entry *ent = &t->entries[i];
        // The strings table only ever has completely empty or full entries.
        if (is_nil(&ent->key) && is_nil(&ent->value))
            return nullptr;

        // We assume ALL valid (i.e: non-nil) keys are strings.
        String *s = as_string(&ent->key);
        if (s->len == len && s->hash == hash) {
            if (cstr_eq(s->data, cs, len))
                return s;
        }
        i = (i + 1) % t->cap;
    }
}
