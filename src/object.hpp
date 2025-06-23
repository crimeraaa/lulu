#pragma once

#include "private.hpp"
#include "value.hpp"
#include "mem.hpp"
#include "slice.hpp"

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

struct Entry {
    Value key, value;
};

struct Table {
    OBJECT_HEADER;
    Slice<Entry> entries;
    size_t       count;   // Number of currently active elements in `entries`.
};

union Object {
    Object_Header base;
    OString       ostring;
    Table         table;
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
