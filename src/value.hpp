#pragma once

#include "lulu.h"
#include "private.hpp"

using Type   = lulu_Type;
using Number = lulu_Number;

struct Value {
    Type type;
    union {
        Number number;
        bool   boolean;
    };
};

inline Type
value_type(Value v)
{
    return v.type;
}

inline const char *
value_type_name(Type t)
{
    switch (t) {
    case LULU_TYPE_NIL:     return "nil";
    case LULU_TYPE_BOOLEAN: return "boolean";
    case LULU_TYPE_NUMBER:  return "number";
    }
    lulu_unreachable();
}

inline const char *
value_type_name(Value v)
{
    return value_type_name(v.type);
}

inline Value
value_make()
{
    Value v{LULU_TYPE_NIL, {0}};
    return v;
}

inline Value
value_make(bool b)
{
    Value v{LULU_TYPE_BOOLEAN, {}};
    v.boolean = b;
    return v;
}

inline Value
value_make(Number n)
{
    Value v{LULU_TYPE_NUMBER, {n}};
    return v;
}

inline bool
value_is_nil(Value v)
{
    return v.type == LULU_TYPE_NIL;
}

inline bool
value_is_boolean(Value v)
{
    return v.type == LULU_TYPE_BOOLEAN;
}

inline bool
value_is_number(Value v)
{
    return v.type == LULU_TYPE_NUMBER;
}

inline bool
value_is_falsy(Value v)
{
    return value_is_nil(v) || (value_is_boolean(v) && !v.boolean);
}

bool
value_eq(Value a, Value b);

void
value_print(Value v);
