#pragma once

#include "private.hpp"

using Type   = lulu_Type;
using Number = lulu_Number;

union  Object;
struct OString;
struct Table;
union  Function;
struct Chunk;

struct Value {
    Value_Type type;
    union {
        Number   number;
        bool     boolean;
        Object  *object;
    };

    constexpr Value(Value_Type t = VALUE_NIL)
        : type{t}
        , number{0}
    {}

    constexpr explicit Value(bool b)
        : type{VALUE_BOOLEAN}
        , boolean{b}
    {}

    constexpr explicit Value(Number n)
        : type{VALUE_NUMBER}
        , number{n}
    {}

    explicit Value(OString *o)
        : type{VALUE_STRING}
        , object{cast(Object *)o}
    {}

    explicit Value(Table *t)
        : type{VALUE_TABLE}
        , object{cast(Object *)t}
    {}

    explicit Value(Function *f)
        : type{VALUE_FUNCTION}
        , object{cast(Object *)f}
    {}

    explicit Value(Chunk *c)
        : type{VALUE_CHUNK}
        , object{cast(Object *)c}
    {}
};

// `value_eq()`.
bool
operator==(Value a, Value b);

inline const char *
value_type_name(Value_Type t)
{
    switch (t) {
    case VALUE_NONE:     return "no value";
    case VALUE_NIL:      return "nil";
    case VALUE_BOOLEAN:  return "boolean";
    case VALUE_NUMBER:   return "number";
    case VALUE_STRING:   return "string";
    case VALUE_TABLE:    return "table";
    case VALUE_FUNCTION: return "function";
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

//=== VALUE TYPE INFORMATION =============================================== {{{

#define value_type(v)           ((v).type)
#define value_is_none(v)        (value_type(v) == VALUE_NONE)
#define value_is_nil(v)         (value_type(v) == VALUE_NIL)
#define value_is_boolean(v)     (value_type(v) == VALUE_BOOLEAN)
#define value_is_number(v)      (value_type(v) == VALUE_NUMBER)
#define value_is_object(v)      (value_type(v) >= VALUE_STRING)
#define value_is_string(v)      (value_type(v) == VALUE_STRING)
#define value_is_table(v)       (value_type(v) == VALUE_TABLE)
#define value_is_function(v)    (value_type(v) == VALUE_FUNCTION)

//=== }}} ======================================================================

//=== VALUE DATA PAYLOADS ================================================== {{{

#define value_to_boolean(v)     ((v).boolean)
#define value_to_number(v)      ((v).number)
#define value_to_object(v)      ((v).object)
#define value_to_ostring(v)     (&value_to_object(v)->ostring)
#define value_to_table(v)       (&value_to_object(v)->table)
#define value_to_function(v)    (&value_to_object(v)->function)

//=== }}} ======================================================================

inline bool
value_is_falsy(Value v)
{
    return value_is_nil(v) || (value_is_boolean(v) && !value_to_boolean(v));
}

void
value_print(Value v);
