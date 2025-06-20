#pragma once

#include "private.hpp"
#include "value.hpp"
#include "mem.hpp"

struct Object {
    Type    type;
    Object *prev;
};

void
object_link(Object **list, Object *o);

void
object_free(lulu_VM &vm, Object *o);

template<class T>
T *
object_new(lulu_VM &vm, Object **list, Type type, size_t extra = 0)
{
    T *o = mem_new<T>(vm, extra);
    o->base.type = type;
    object_link(list, &o->base);
    return o;
}
