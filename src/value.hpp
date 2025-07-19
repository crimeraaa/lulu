#pragma once

#include "private.hpp"
#include "string.hpp"   // OString

union  Object;
struct Table;
union  Closure;
struct Chunk;

struct LULU_PRIVATE Value {

private:
    Value_Type m_type;
    union {
        Number   number;
        bool     boolean;
        Object  *object;
        void    *pointer; // light userdata.
    };

public:
    constexpr
    Value(Value_Type t = VALUE_NIL)
        : m_type{t}
        , number{0}
    {}

    constexpr
    Value(bool b)
        : m_type{VALUE_BOOLEAN}
        , boolean{b}
    {}

    constexpr
    Value(Number n)
        : m_type{VALUE_NUMBER}
        , number{n}
    {}

    constexpr void
    operator=(bool b)
    {
        this->m_type    = VALUE_BOOLEAN;
        this->boolean = b;
    }

    constexpr void
    operator=(Number n)
    {
        this->m_type   = VALUE_NUMBER;
        this->number = n;
    }

    static constexpr Value
    make_userdata(void *p)
    {
        Value v;
        v.m_type    = VALUE_USERDATA;
        v.pointer = p;
        return v;
    }

    static Value
    make_string(OString *s)
    {
        Value v;
        v.m_type   = VALUE_STRING;
        v.object = cast(Object *)s;
        return v;
    }

    static Value
    make_table(Table *t)
    {
        Value v;
        v.m_type = VALUE_TABLE;
        v.object = cast(Object *)t;
        return v;
    }

    static Value
    make_function(Closure *f)
    {
        Value v;
        v.m_type = VALUE_FUNCTION;
        v.object = cast(Object *)f;
        return v;
    }

    static Value
    make_chunk(Chunk *c)
    {
        Value v;
        v.m_type = VALUE_CHUNK;
        v.object = cast(Object *)c;
        return v;
    }

    bool
    operator==(Value other) const;


    //=== VALUE TYPE INFORMATION =========================================== {{{

    static const char *
    type_name(Value_Type t)
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

    const char *
    type_name() const
    {
        return Value::type_name(this->type());
    }

    constexpr Value_Type
    type() const noexcept
    {
        return this->m_type;
    }

    constexpr bool
    is_none() const noexcept
    {
        return this->type() == VALUE_NONE;
    }

    constexpr bool
    is_nil() const noexcept
    {
        return this->type() == VALUE_NIL;
    }

    constexpr bool
    is_boolean() const noexcept
    {
        return this->type() == VALUE_BOOLEAN;
    }

    constexpr bool
    is_number() const noexcept
    {
        return this->type() == VALUE_NUMBER;
    }

    constexpr bool
    is_userdata() const noexcept
    {
        return this->type() == VALUE_USERDATA;
    }

    constexpr bool
    is_object() const noexcept
    {
        return this->type() >= VALUE_STRING;
    }

    constexpr bool
    is_string() const noexcept
    {
        return this->type() == VALUE_STRING;
    }

    constexpr bool
    is_table() const noexcept
    {
        return this->type() == VALUE_TABLE;
    }

    constexpr bool
    is_function() const noexcept
    {
        return this->type() == VALUE_FUNCTION;
    }

    //=== }}} =================================================================

    //=== VALUE DATA PAYLOADS ============================================== {{{

    bool
    to_boolean() const
    {
        lulu_assert(this->is_boolean());
        return this->boolean;
    }

    Number
    to_number() const
    {
        lulu_assert(this->is_number());
        return this->number;
    }

    void *
    to_userdata() const
    {
        lulu_assert(this->is_userdata());
        return this->pointer;
    }

    Object *
    to_object() const noexcept
    {
        return this->object;
    }

    //=== }}} ==================================================================

    inline bool
    is_falsy() const noexcept
    {
        return this->is_nil() || (this->is_boolean() && !this->to_boolean());
    }

    inline OString *
    to_ostring() const;

    inline LString
    to_lstring() const;

    inline const char *
    to_cstring() const;

    inline Table *
    to_table() const;

    inline Closure *
    to_function() const;

    inline void *
    to_pointer() const;
};

constexpr Value
nil = {};

LULU_FUNC void
value_print(Value v);
