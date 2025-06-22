#pragma once

#include "private.hpp"
#include "value.hpp"
#include "mem.hpp"

struct Object {
    Type    type;
    Object *next;
};

void
object_free(lulu_VM &vm, Object *o);

template<class T>
inline T *
object_new(lulu_VM &vm, Object **list, Type type, size_t extra = 0)
{
    T *o = mem_new<T>(vm, extra);
    o->base.type = type;
    o->base.next = *list; // Chain the new object.
    *list = &o->base;
    return o;
}
