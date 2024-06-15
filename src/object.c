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

ToNumber luluVal_to_number(const Value *vl)
{
    ToNumber conv;
    conv.ok = false;
    if (is_number(vl)) {
        conv.number = as_number(vl);
        conv.ok = true;
    }
    if (is_string(vl)) {
        char      *end;
        String    *s  = as_string(vl);
        StringView sv = sv_create_from_len(s->data, s->len);
        Number     n  = cstr_tonumber(sv.begin, &end);
        if (end == sv.end) {
            conv.number = n;
            conv.ok     = true;
        }
    }
    return conv;
}

const char *luluVal_to_cstring(const Value *vl, char *buf, int *out)
{
    int len = 0;
    if (out != NULL)
        *out = -1;
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
    if (out != NULL)
        *out = len;
    return buf;
}

void luluVal_print_value(const Value *vl, bool isdebug)
{
    if (is_string(vl) && isdebug) {
        const String *s = as_string(vl);
        if (s->len <= 1)
            printf("\'%s\'", s->data);
        else
            printf("\"%s\"", s->data);
    } else {
        char buf[MAX_TOSTRING];
        printf("%s", luluVal_to_cstring(vl, buf, NULL));
    }
}

bool luluVal_equal(const Value *a, const Value *b)
{
    // Logically, differing types can never be equal.
    if (get_tag(a) != get_tag(b))
        return false;
    switch (get_tag(a)) {
    case TYPE_NIL:      return true;
    case TYPE_BOOLEAN:  return as_boolean(a) == as_boolean(b);
    case TYPE_NUMBER:   return num_eq(as_number(a), as_number(b));
    case TYPE_STRING:   // We assume all objects are correctly interned.
    case TYPE_TABLE:    return as_object(a) == as_object(b);
    }
}

void luluVal_init_array(VArray *va)
{
    va->values = NULL;
    va->len    = 0;
    va->cap    = 0;
}

void luluVal_free_array(lulu_VM *vm, VArray *va)
{
    luluMem_free_parray(vm, va->values, va->len);
    luluVal_init_array(va);
}

void luluVal_write_array(lulu_VM *vm, VArray *va, const Value *vl)
{
    if (va->len + 1 > va->cap) {
        int oldcap = va->cap;
        int newcap = luluMem_grow_capacity(oldcap);
        va->values = luluMem_resize_parray(vm, va->values, oldcap, newcap);
        va->cap    = newcap;
    }
    va->values[va->len] = *vl;
    va->len += 1;
}
