#include "object.h"
#include "limits.h"
#include "memory.h"
#include "string.h"
#include "table.h"
#include "vm.h"
#include "api.h"

const View LULU_TYPENAMES[] = {
    [TYPE_NIL]     = view_from_lit("nil"),
    [TYPE_BOOLEAN] = view_from_lit("boolean"),
    [TYPE_NUMBER]  = view_from_lit("number"),
    [TYPE_STRING]  = view_from_lit("string"),
    [TYPE_TABLE]   = view_from_lit("table"),
};

void luluVal_intern_typenames(lulu_VM *vm)
{
    for (size_t i = 0; i < array_len(LULU_TYPENAMES); i++) {
        luluStr_copy(vm, LULU_TYPENAMES[i]);
    }
}

ToNumber luluVal_to_number(const Value *val)
{
    ToNumber conv;
    if (is_number(val)) {
        conv.number = as_number(val);
        conv.ok     = true;
        return conv;
    }
    if (is_string(val)) {
        char   *end;
        String *s  = as_string(val);
        View    sv = view_from_len(s->data, s->len);
        Number  n  = cstr_tonumber(sv.begin, &end);
        if (end == sv.end) {
            conv.number = n;
            conv.ok     = true;
            return conv;
        }
    }
    conv.ok = false;
    return conv;
}

const char *luluVal_to_cstring(const Value *val, char *buf)
{
    size_t len = 0;
    switch (get_tag(val)) {
    case TYPE_NIL:
        return "nil";
    case TYPE_BOOLEAN:
        return as_boolean(val) ? "true" : "false";
    case TYPE_NUMBER:
        len = lulu_num_tostring(buf, as_number(val));
        break;
    case TYPE_STRING:
        return as_string(val)->data;
    case TYPE_TABLE:
        len = sprintf(buf, "%s: %p", get_typename(val), as_pointer(val));
        break;
    }
    buf[len] = '\0';
    return buf;
}

bool luluVal_equal(const Value *a, const Value *b)
{
    // Logically, differing types can never be equal.
    if (get_tag(a) != get_tag(b))
        return false;
    switch (get_tag(a)) {
    case TYPE_NIL:      return true;
    case TYPE_BOOLEAN:  return as_boolean(a) == as_boolean(b);
    case TYPE_NUMBER:   return lulu_num_eq(as_number(a), as_number(b));
    case TYPE_STRING:   // We assume all objects are correctly interned.
    case TYPE_TABLE:    return as_object(a) == as_object(b);
    }
}

void luluVal_init_array(Array *arr)
{
    arr->values = nullptr;
    arr->len    = 0;
    arr->cap    = 0;
}

void luluVal_free_array(lulu_VM *vm, Array *arr)
{
    luluMem_free_parray(vm, arr->values, arr->len);
    luluVal_init_array(arr);
}

void luluVal_write_array(lulu_VM *vm, Array *arr, const Value *val)
{
    if (arr->len + 1 > arr->cap) {
        int oldcap  = arr->cap;
        int newcap  = luluMem_grow_capacity(oldcap);
        arr->values = luluMem_resize_parray(vm, arr->values, oldcap, newcap);
        arr->cap    = newcap;
    }
    arr->values[arr->len] = *val;
    arr->len += 1;
}
