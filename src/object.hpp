#pragma once

#include "private.hpp"
#include "value.hpp"
#include "mem.hpp"
#include "slice.hpp"
#include "string.hpp"
#include "table.hpp"
#include "chunk.hpp"
#include "function.hpp"

struct Object_Header {
    OBJECT_HEADER;
};

union Object {
    Object_Header base;
    OString       ostring;
    Table         table;
    Chunk         chunk;
    Closure      function;
};

LULU_FUNC inline OString *
value_to_ostring(Value v)
{
    lulu_assert(value_is_string(v));
    return &value_to_object(v)->ostring;
}

LULU_FUNC inline LString
value_to_lstring(Value v)
{
    return ostring_to_lstring(value_to_ostring(v));
}

LULU_FUNC inline const char *
value_to_cstring(Value v)
{
    return ostring_to_cstring(value_to_ostring(v));
}

LULU_FUNC inline Table *
value_to_table(Value v)
{
    lulu_assert(value_is_table(v));
    return &value_to_object(v)->table;
}

LULU_FUNC inline Closure *
value_to_function(Value v)
{
    lulu_assert(value_is_function(v));
    return &value_to_object(v)->function;
}

LULU_FUNC void
object_free(lulu_VM *vm, Object *o);

template<class T>
LULU_FUNC inline T *
object_new(lulu_VM *vm, Object **list, Value_Type type, isize extra = 0)
{
    T *o = mem_new<T>(vm, extra);
    o->type = type;

    // Chain the new object.
    o->next = *list;
    *list   = cast(Object *)o;
    return o;
}
