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

    constexpr
    Value(Value_Type t = VALUE_NIL)
        : type{t}
        , number{0}
    {}

    constexpr
    Value(bool b)
        : type{VALUE_BOOLEAN}
        , boolean{b}
    {}

    constexpr
    Value(Number n)
        : type{VALUE_NUMBER}
        , number{n}
    {}

    constexpr void
    operator=(bool b)
    {
        this->type    = VALUE_BOOLEAN;
        this->boolean = b;
    }

    constexpr void
    operator=(Number n)
    {
        this->type   = VALUE_NUMBER;
        this->number = n;
    }
};

constexpr Value
nil = {};

LULU_FUNC constexpr Value
value_make_userdata(void *p)
{
    Value v;
    v.type    = VALUE_USERDATA;
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

LULU_FUNC constexpr Value_Type
value_type(Value v)
{
    return v.type;
}

LULU_FUNC constexpr bool
value_is_none(Value v)
{
    return value_type(v) == VALUE_NONE;
}

LULU_FUNC constexpr bool
value_is_nil(Value v)
{
    return value_type(v) == VALUE_NIL;
}

LULU_FUNC constexpr bool
value_is_boolean(Value v)
{
    return value_type(v) == VALUE_BOOLEAN;
}

LULU_FUNC constexpr bool
value_is_number(Value v)
{
    return value_type(v) == VALUE_NUMBER;
}

LULU_FUNC constexpr bool
value_is_userdata(Value v)
{
    return value_type(v) == VALUE_USERDATA;
}

LULU_FUNC constexpr bool
value_is_object(Value v)
{
    return value_type(v) >= VALUE_STRING;
}

LULU_FUNC constexpr bool
value_is_string(Value v)
{
    return value_type(v) == VALUE_STRING;
}

LULU_FUNC constexpr bool
value_is_table(Value v)
{
    return value_type(v) == VALUE_TABLE;
}

LULU_FUNC constexpr bool
value_is_function(Value v)
{
    return value_type(v) == VALUE_FUNCTION;
}

//=== }}} ======================================================================

//=== VALUE DATA PAYLOADS ================================================== {{{

LULU_FUNC inline bool
value_to_boolean(Value v)
{
    return check_expr(value_is_boolean(v), v.boolean);
}

LULU_FUNC inline Number
value_to_number(Value v)
{
    return check_expr(value_is_number(v), v.number);
}

LULU_FUNC inline void *
value_to_userdata(Value v)
{
    return check_expr(value_is_userdata(v), v.pointer);
}

LULU_FUNC inline Object *
value_to_object(Value v)
{
    return check_expr(value_is_object(v), v.object);
}

//=== }}} ======================================================================

LULU_FUNC inline bool
value_is_falsy(Value v)
{
    return value_is_nil(v) || (value_is_boolean(v) && !value_to_boolean(v));
}

LULU_FUNC void
value_print(Value v);
