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
};

constexpr Value
nil{VALUE_NIL, {0}};

LULU_FUNC constexpr Value
value_make_boolean(bool b)
{
    Value v{VALUE_BOOLEAN, {0}};
    v.boolean = b;
    return v;
}

LULU_FUNC constexpr Value
value_make_number(Number n)
{
    Value v{VALUE_NUMBER, {0}};
    v.number = n;
    return v;
}

LULU_FUNC constexpr Value
value_make_userdata(void *p)
{
    Value v{VALUE_USERDATA, {0}};
    v.pointer = p;
    return v;
}

LULU_FUNC inline Value
value_make_string(OString *s)
{
    Value v;
    v.type   = VALUE_STRING;
    v.object = cast(Object *)s;
    return v;
}

LULU_FUNC inline Value
value_make_table(Table *t)
{
    Value v;
    v.type   = VALUE_TABLE;
    v.object = cast(Object *)t;
    return v;
}

LULU_FUNC inline Value
value_make_function(Closure *f)
{
    Value v;
    v.type   = VALUE_FUNCTION;
    v.object = cast(Object *)f;
    return v;
}

LULU_FUNC inline Value
value_make_chunk(Chunk *c)
{
    Value v;
    v.type   = VALUE_CHUNK;
    v.object = cast(Object *)c;
    return v;
}

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
