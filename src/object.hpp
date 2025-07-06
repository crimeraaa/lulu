#pragma once

#include "private.hpp"
#include "value.hpp"
#include "mem.hpp"
#include "slice.hpp"
#include "string.hpp"
#include "table.hpp"
#include "chunk.hpp"
#include "function.hpp"

#define value_to_lstring(v)     ostring_to_lstring(value_to_ostring(v))
#define value_to_cstring(v)     ostring_to_cstring(value_to_ostring(v))

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
