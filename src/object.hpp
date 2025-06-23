#pragma once

#include "private.hpp"
#include "value.hpp"
#include "mem.hpp"

#define OBJECT_HEADER Value_Type type; Object *next

struct Object_Header {
    OBJECT_HEADER;
};

struct OString {
    OBJECT_HEADER;
    size_t len;
    u32    hash;
    char   data[1];
};

union Object {
    Object_Header base;
    OString       ostring;
};

void
object_free(lulu_VM &vm, Object *o);

template<class T>
inline T *
object_new(lulu_VM &vm, Object **list, Value_Type type, size_t extra = 0)
{
    T *o = mem_new<T>(vm, extra);
    o->type = type;
    o->next = *list; // Chain the new object.
    *list = cast(Object *)o;
    return o;
}
