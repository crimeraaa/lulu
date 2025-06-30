#pragma once

#include "private.hpp"

union  Object;
struct OString;
struct Table;
union  Closure;
struct Chunk;

struct Value {
    Value_Type type;
    union {
        Number   number;
        bool     boolean;
        Object  *object;
        void    *pointer; // light userdata.
    };

    LULU_PRIVATE
    constexpr
    Value(Value_Type t = VALUE_NIL)
        : type{t}
        , number{0}
    {}

    LULU_PRIVATE
    constexpr explicit
    Value(bool b)
        : type{VALUE_BOOLEAN}
        , boolean{b}
    {}

    LULU_PRIVATE
    constexpr explicit
    Value(Number n)
        : type{VALUE_NUMBER}
        , number{n}
    {}

    LULU_PRIVATE
    constexpr explicit
    Value(void *p)
        : type{VALUE_USERDATA}
        , pointer{p}
    {}

    LULU_PRIVATE
    explicit
    Value(OString *o)
        : type{VALUE_STRING}
        , object{cast(Object *)o}
    {}

    LULU_PRIVATE
    explicit
    Value(Table *t)
        : type{VALUE_TABLE}
        , object{cast(Object *)t}
    {}

    LULU_PRIVATE
    explicit
    Value(Closure *f)
        : type{VALUE_FUNCTION}
        , object{cast(Object *)f}
    {}

    LULU_PRIVATE
    explicit
    Value(Chunk *c)
        : type{VALUE_CHUNK}
        , object{cast(Object *)c}
    {}
};

// `value_eq()`.
LULU_FUNC bool
operator==(Value a, Value b);

LULU_FUNC inline const char *
value_type_name(Value_Type t)
{
    switch (t) {
    case VALUE_NONE:        return "no value";
    case VALUE_NIL:         return "nil";
    case VALUE_BOOLEAN:     return "boolean";
    case VALUE_NUMBER:      return "number";
    case VALUE_USERDATA:    return "lightuserdata";
    case VALUE_STRING:      return "string";
    case VALUE_TABLE:       return "table";
    case VALUE_FUNCTION:    return "function";
    case VALUE_CHUNK:
        break;
    }
    lulu_unreachable();
}

LULU_FUNC inline const char *
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
#define value_is_userdata(v)    (value_type(v) == VALUE_USERDATA)
#define value_is_object(v)      (value_type(v) >= VALUE_STRING)
#define value_is_string(v)      (value_type(v) == VALUE_STRING)
#define value_is_table(v)       (value_type(v) == VALUE_TABLE)
#define value_is_function(v)    (value_type(v) == VALUE_FUNCTION)

//=== }}} ======================================================================

//=== VALUE DATA PAYLOADS ================================================== {{{

#define value_to_boolean(v)     ((v).boolean)
#define value_to_number(v)      ((v).number)
#define value_to_userdata(v)    ((v).pointer)
#define value_to_object(v)      ((v).object)
#define value_to_ostring(v)     (&value_to_object(v)->ostring)
#define value_to_table(v)       (&value_to_object(v)->table)
#define value_to_function(v)    (&value_to_object(v)->function)

//=== }}} ======================================================================

LULU_FUNC inline bool
value_is_falsy(Value v)
{
    return value_is_nil(v) || (value_is_boolean(v) && !value_to_boolean(v));
}

LULU_FUNC void
value_print(Value v);
