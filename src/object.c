#include "object.h"
#include "limits.h"
#include "memory.h"
#include "string.h"
#include "table.h"
#include "vm.h"

const LString LULU_TYPENAMES[] = {
    [TYPE_NIL]     = lstr_from_lit("nil"),
    [TYPE_BOOLEAN] = lstr_from_lit("boolean"),
    [TYPE_NUMBER]  = lstr_from_lit("number"),
    [TYPE_STRING]  = lstr_from_lit("string"),
    [TYPE_TABLE]   = lstr_from_lit("table"),
};

void luluVal_intern_typenames(lulu_VM *vm)
{
    for (size_t i = 0; i < array_len(LULU_TYPENAMES); i++) {
        luluStr_copy(vm, LULU_TYPENAMES[i].string, LULU_TYPENAMES[i].length);
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
        String *s = as_string(val);
        Number  n = cstr_tonumber(s->data, &end);
        if (end == (s->data + s->length)) {
            conv.number = n;
            conv.ok     = true;
            return conv;
        }
    }
    conv.ok = false;
    return conv;
}

const char *luluVal_to_string(const Value *val, char *buf)
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

void luluVal_init_array(Array *a)
{
    a->values   = nullptr;
    a->length   = 0;
    a->capacity = 0;
}

void luluVal_free_array(lulu_VM *vm, Array *a)
{
    luluMem_free_parray(vm, a->values, a->length);
    luluVal_init_array(a);
}

void luluVal_resize_array(lulu_VM *vm, Array *a, int n)
{
    a->values   = luluMem_resize_parray(vm, a->values, a->capacity, n);
    a->capacity = n;
}

void luluVal_write_array(lulu_VM *vm, Array *a, const Value *val)
{
    if (a->length + 1 > a->capacity)
        luluVal_resize_array(vm, a, luluMem_grow_capacity(a->capacity));
    a->values[a->length] = *val;
    a->length += 1;
}
