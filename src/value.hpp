#pragma once

#include "lulu.h"
#include "private.hpp"

using Type   = lulu_Type;
using Number = lulu_Number;

enum Value_Type {
    VALUE_NIL       = LULU_TYPE_NIL,
    VALUE_BOOLEAN   = LULU_TYPE_BOOLEAN,
    VALUE_NUMBER    = LULU_TYPE_NUMBER,
    VALUE_STRING    = LULU_TYPE_STRING,

    // Not accessible from user code.
    VALUE_CHUNK,
};

struct Value {
    Value_Type type;
    union {
        Number   number;
        bool     boolean;
        Object  *object;
    };

    Value()
        : type{VALUE_NIL}
        , number{0}
    {}

    explicit Value(bool b)
        : type{VALUE_BOOLEAN}
        , boolean{b}
    {}

    explicit Value(Number n)
        : type{VALUE_NUMBER}
        , number{n}
    {}

    Value(OString *o)
        : type{VALUE_STRING}
        , object{cast(Object *)o}
    {}
};

// `value_eq()`.
bool
operator==(Value a, Value b);

inline Value_Type
value_type(Value v)
{
    return v.type;
}

inline const char *
value_type_name(Value_Type t)
{
    switch (t) {
    case VALUE_NIL:     return "nil";
    case VALUE_BOOLEAN: return "boolean";
    case VALUE_NUMBER:  return "number";
    case VALUE_STRING:  return "string";
    case VALUE_CHUNK:
        break;
    }
    lulu_unreachable();
}

inline const char *
value_type_name(Value v)
{
    return value_type_name(v.type);
}

inline bool
value_is_nil(Value v)
{
    return v.type == VALUE_NIL;
}

inline bool
value_is_boolean(Value v)
{
    return v.type == VALUE_BOOLEAN;
}

inline bool
value_is_number(Value v)
{
    return v.type == VALUE_NUMBER;
}

inline bool
value_is_string(Value v)
{
    return v.type == VALUE_STRING;
}

inline bool
value_is_falsy(Value v)
{
    return value_is_nil(v) || (value_is_boolean(v) && !v.boolean);
}

void
value_print(Value v);
