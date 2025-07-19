#pragma once

#include "private.hpp"
#include "string.hpp"   // OString

union  Object;
struct Table;
union  Closure;
struct Chunk;

struct LULU_PRIVATE Value {

private:
    // Later on, if we decide to incorporate NaN-boxing/Pointer-tagging, we can
    // use macros to change what members are to be included in the struct.
    Value_Type m_type;
    union {
        Number   m_number;
        bool     m_boolean;
        Object  *m_object;
        void    *m_pointer; // light userdata.
    };

public:
    // @note 2025-07-19 Affected by Nan-boxing/Pointer-tagging
    constexpr
    Value(Value_Type t = VALUE_NIL)
        : m_type{t}
        , m_number{0}
    {}

    // @note 2025-07-19 Affected by Nan-boxing/Pointer-tagging
    constexpr
    Value(bool b)
        : m_type{VALUE_BOOLEAN}
        , m_boolean{b}
    {}

    // @note 2025-07-19 Affected by Nan-boxing/Pointer-tagging
    constexpr
    Value(Number n)
        : m_type{VALUE_NUMBER}
        , m_number{n}
    {}

    // @note 2025-07-19 Affected by Nan-boxing/Pointer-tagging
    constexpr void
    operator=(bool b)
    {
        this->m_type    = VALUE_BOOLEAN;
        this->m_boolean = b;
    }

    // @note 2025-07-19 Affected by Nan-boxing/Pointer-tagging
    constexpr void
    operator=(Number n)
    {
        this->m_type   = VALUE_NUMBER;
        this->m_number = n;
    }

    // @note 2025-07-19 Affected by Nan-boxing/Pointer-tagging
    static constexpr Value
    make_userdata(void *p)
    {
        Value v;
        v.m_type  = VALUE_USERDATA;
        v.m_pointer = p;
        return v;
    }

    // @note 2025-07-19 Affected by Nan-boxing/Pointer-tagging
    static Value
    make_string(OString *s)
    {
        Value v;
        v.m_type = VALUE_STRING;
        v.m_object = cast(Object *)s;
        return v;
    }

    // @note 2025-07-19 Affected by Nan-boxing/Pointer-tagging
    static Value
    make_table(Table *t)
    {
        Value v;
        v.m_type   = VALUE_TABLE;
        v.m_object = cast(Object *)t;
        return v;
    }

    // @note 2025-07-19 Affected by Nan-boxing/Pointer-tagging
    static Value
    make_function(Closure *f)
    {
        Value v;
        v.m_type   = VALUE_FUNCTION;
        v.m_object = cast(Object *)f;
        return v;
    }

    // @note 2025-07-19 Affected by Nan-boxing/Pointer-tagging
    static Value
    make_chunk(Chunk *c)
    {
        Value v;
        v.m_type = VALUE_CHUNK;
        v.m_object = cast(Object *)c;
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
        // You cannot, and should not, call this function with payloads of
        // non-user facing types, such as `Chunk *`.
        lulu_unreachable();
        return nullptr;
    }

    const char *
    type_name() const
    {
        return Value::type_name(this->type());
    }

    // @note 2025-07-19 Affected by Nan-boxing/Pointer-tagging
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

    // @note 2025-07-19 Affected by Nan-boxing/Pointer-tagging
    bool
    to_boolean() const
    {
        lulu_assert(this->is_boolean());
        return this->m_boolean;
    }

    // @note 2025-07-19 Affected by Nan-boxing/Pointer-tagging
    Number
    to_number() const
    {
        lulu_assert(this->is_number());
        return this->m_number;
    }

    // @note 2025-07-19 Affected by Nan-boxing/Pointer-tagging
    void *
    to_userdata() const
    {
        lulu_assert(this->is_userdata());
        return this->m_pointer;
    }

    // @note 2025-07-19 Affected by Nan-boxing/Pointer-tagging
    Object *
    to_object() const noexcept
    {
        return this->m_object;
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
