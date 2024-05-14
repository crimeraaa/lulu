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

uint32_t hash_string(StrView view)
{
    uint32_t hash = FNV1A_OFFSET32;
    char     prev = 0;
    for (const char *ptr = view.begin; ptr < view.end; ptr++) {
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

uint32_t hash_rstring(StrView view)
{
    uint32_t hash = FNV1A_OFFSET32;
    for (const char *ptr = view.begin; ptr < view.end; ptr++) {
        hash ^= cast(Byte, *ptr);
        hash *= FNV1A_PRIME32;
    }
    return hash;
}

String *new_string(int len, Alloc *alloc)
{
    // Note how we add 1 for the nul char.
    size_t  need = string_size(len + 1);
    String *inst = cast(String*, new_object(need, TYPE_STRING, alloc));
    inst->len    = len;
    return inst;
}

// Note we add 1 to `oldsz` because we previously allocated 1 extra by for nul.
void free_string(String *self, Alloc *alloc)
{
    free_pointer(self, string_size(self->len + 1), alloc);
}

static void build_string(String *self, StrView view)
{
    char   *end   = self->data; // For loop counter may skip.
    int     skips = 0;          // Number escape characters emitted.
    char    prev  = 0;

    for (const char *ptr = view.begin; ptr < view.end; ptr++) {
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
    *end      = '\0';
    self->len = view.len - skips;
}

static void end_string(String *self, uint32_t hash)
{
    self->data[self->len] = '\0';
    self->hash            = hash;
}

static String *copy_string_or_rstring(VM *vm, StrView view, bool israw)
{
    Alloc   *alloc    = &vm->alloc;
    uint32_t hash     = (israw) ? hash_rstring(view) : hash_string(view);
    String  *interned = find_interned(vm, view, hash);

    // Is this string already interned?
    if (interned != NULL) {
        return interned;
    }

    String *inst = new_string(view.len, alloc);
    if (israw) {
        memcpy(inst->data, view.begin, view.len);
    } else {
        build_string(inst, view);
    }
    end_string(inst, hash);

    // If we have escapes, are we really REALLY sure this isn't interned?
    if (inst->len != view.len) {
        interned = find_interned(vm, make_strview(inst->data, inst->len), hash);
        if (interned != NULL) {
            remove_object(&vm->objects, &inst->object);
            free_string(inst, alloc);
            return interned;
        }
    }
    set_interned(vm, inst);
    return inst;
}

String *copy_rstring(VM *vm, StrView view)
{
    return copy_string_or_rstring(vm, view, true);
}

String *copy_string(VM *vm, StrView view)
{
    return copy_string_or_rstring(vm, view, false);
}

String *concat_strings(VM *vm, int argc, const Value argv[], int len)
{
    Alloc  *alloc  = &vm->alloc;
    String *inst   = new_string(len, alloc);
    StrView view   = make_strview(inst->data, inst->len);
    int     offset = 0;

    // We already built each individual string so no need to interpret escapes.
    for (int i = 0; i < argc; i++) {
        const String *arg = as_string(&argv[i]);
        memcpy(inst->data + offset, arg->data, arg->len);
        offset += arg->len;
    }
    end_string(inst, hash_string(view));
    String *interned = find_interned(vm, view, inst->hash);
    if (interned != NULL) {
        remove_object(&vm->objects, &inst->object);
        free_string(inst, alloc);
        return interned;
    }
    set_interned(vm, inst);
    return inst;
}
