#include "object.h"
#include "limits.h"
#include "memory.h"
#include "string.h"
#include "table.h"
#include "vm.h"
#include "api.h"

const char *const LULU_TYPENAMES[] = {
    [TYPE_NIL]     = "nil",
    [TYPE_BOOLEAN] = "boolean",
    [TYPE_NUMBER]  = "number",
    [TYPE_STRING]  = "string",
    [TYPE_TABLE]   = "table",
};

const Value *value_tonumber(Value *vl)
{
    if (is_number(vl)) {
        return vl;
    }
    if (is_string(vl)) {
        char   *end;
        String *s  = as_string(vl);
        StrView sv = sv_inst(s->data, s->len);
        Number  n  = cstr_tonumber(sv.begin, &end);
        if (end == sv.end) {
            setv_number(vl, n);
            return vl;
        }
    }
    return NULL;
}

const char *value_tocstring(const Value *vl, char *buf, int *out)
{
    int len = 0;
    if (out != NULL) {
        *out = -1;
    }
    switch (get_tag(vl)) {
    case TYPE_NIL:
        return "nil";
    case TYPE_BOOLEAN:
        return as_boolean(vl) ? "true" : "false";
    case TYPE_NUMBER:
        len = num_tostring(buf, as_number(vl));
        break;
    case TYPE_STRING:
        return as_cstring(vl);
    case TYPE_TABLE:
        len = snprintf(buf,
                       MAX_TOSTRING,
                       "%s: %p",
                       get_typename(vl),
                       as_pointer(vl));
        break;
    }
    buf[len] = '\0';
    if (out != NULL) {
        *out = len;
    }
    return buf;
}

void print_value(const Value *vl, bool isdebug)
{
    if (is_string(vl) && isdebug) {
        const String *s = as_string(vl);
        if (s->len <= 1)
            printf("\'%s\'", s->data);
        else
            printf("\"%s\"", s->data);
    } else {
        char buf[MAX_TOSTRING];
        printf("%s", value_tocstring(vl, buf, NULL));
    }
}

bool values_equal(const Value *a, const Value *b)
{
    // Logically, differing types can never be equal.
    if (get_tag(a) != get_tag(b)) {
        return false;
    }
    switch (get_tag(a)) {
    case TYPE_NIL:      return true;
    case TYPE_BOOLEAN:  return as_boolean(a) == as_boolean(b);
    case TYPE_NUMBER:   return num_eq(as_number(a), as_number(b));
    case TYPE_STRING:   // We assume all objects are correctly interned.
    case TYPE_TABLE:    return as_object(a) == as_object(b);
    }
}

void init_varray(VArray *va)
{
    va->values = NULL;
    va->len    = 0;
    va->cap    = 0;
}

void free_varray(VArray *va, Alloc *al)
{
    free_parray(va->values, va->len, al);
    init_varray(va);
}

void write_varray(VArray *va, const Value *vl, Alloc *al)
{
    if (va->len + 1 > va->cap) {
        int oldcap = va->cap;
        int newcap = grow_capacity(oldcap);
        va->values = resize_parray(va->values, oldcap, newcap, al);
        va->cap    = newcap;
    }
    va->values[va->len] = *vl;
    va->len += 1;
}

void set_interned(lulu_VM *vm, const String *s)
{
    Alloc *al = &vm->allocator;
    Table *t  = &vm->strings;
    Value  k  = make_string(s);
    Value  v  = make_boolean(true);
    set_table(t, &k, &v, al);
}

String *find_interned(lulu_VM *vm, StrView sv, uint32_t hash)
{
    Table *t = &vm->strings;
    if (t->count == 0) {
        return NULL;
    }
    uint32_t i = hash % t->cap;
    for (;;) {
        Entry *ent = &t->entries[i];
        // The strings table only ever has completely empty or full entries.
        if (is_nil(&ent->key) && is_nil(&ent->value)) {
            return NULL;
        }
        // We assume ALL valid (i.e: non-nil) keys are strings.
        String *s = as_string(&ent->key);
        if (s->len == sv.len && s->hash == hash) {
            if (cstr_eq(s->data, sv.begin, sv.len)) {
                return s;
            }
        }
        i = (i + 1) % t->cap;
    }
}
