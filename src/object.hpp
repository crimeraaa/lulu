#pragma once

#include "private.hpp"
#include "value.hpp"
#include "mem.hpp"
#include "slice.hpp"
#include "string.hpp"
#include "table.hpp"
#include "chunk.hpp"
#include "function.hpp"

#define value_to_string(v)      ostring_to_string(value_to_ostring(v))
#define value_to_cstring(v)     ostring_to_cstring(value_to_ostring(v))
#define value_to_c_closure(v)   (&value_to_function(v)->c)
#define value_to_lua_closure(v) (&value_to_function(v)->l)

#define value_is_c_closure(v)   closure_is_c(value_to_function(v))
#define value_is_lua_closure(v) closure_is_lua(value_to_function(v))

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
