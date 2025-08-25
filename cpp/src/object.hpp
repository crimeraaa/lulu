#pragma once

#include "chunk.hpp"
#include "function.hpp"
#include "mem.hpp"
#include "private.hpp"
#include "slice.hpp"
#include "string.hpp"
#include "table.hpp"
#include "value.hpp"

union Object {
    struct Header {
        OBJECT_HEADER;
    };

    Header  base;
    OString ostring;
    Table   table;
    Chunk   chunk;
    Closure function;
};

inline OString *
Value::to_ostring() const
{
    lulu_assert(this->is_string());
    return &this->to_object()->ostring;
}

inline LString
Value::to_lstring() const
{
    return this->to_ostring()->to_lstring();
}

inline const char *
Value::to_cstring() const
{
    return this->to_ostring()->to_cstring();
}

inline Table *
Value::to_table() const
{
    lulu_assert(this->is_table());
    return &this->to_object()->table;
}

inline Closure *
Value::to_function() const
{
    lulu_assert(this->is_function());
    return &this->to_object()->function;
}

inline void *
Value::to_pointer() const
{
    switch (this->type()) {
    case VALUE_LIGHTUSERDATA:
        return this->to_userdata();
    case VALUE_TABLE:
        return this->to_table();
    case VALUE_FUNCTION:
        return this->to_function();

    default:
        break;
    }
    return nullptr;
}

void
object_free(lulu_VM *vm, Object *o);

template<class T>
inline T *
object_new(lulu_VM *vm, Object **list, Value_Type type, isize extra = 0)
{
    T *o    = mem_new<T>(vm, extra);
    o->type = type;

    // Chain the new object.
    o->next = *list;
    *list   = reinterpret_cast<Object *>(o);
    return o;
}
