#pragma once

#include "private.hpp"
#include "value.hpp"
#include "mem.hpp"
#include "slice.hpp"
#include "string.hpp"
#include "table.hpp"
#include "chunk.hpp"

#define value_to_string(v)  ostring_to_string(value_to_ostring(v))
#define value_to_cstring(v) ostring_to_cstring(value_to_ostring(v))

struct Object_Header {
    OBJECT_HEADER;
};

union Object {
    Object_Header base;
    OString       ostring;
    Table         table;
    Chunk         chunk;
};

void
object_free(lulu_VM &vm, Object *o);

template<class T>
inline T *
object_new(lulu_VM &vm, Object **list, Value_Type type, size_t extra = 0)
{
    T *o = mem_new<T>(vm, extra);
    o->type = type;

    // Chain the new object.
    o->next = *list;
    *list   = cast(Object *)o;
    return o;
}
