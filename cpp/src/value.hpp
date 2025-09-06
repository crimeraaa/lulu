#pragma once

#include "private.hpp"
#include "slice.hpp" // LString

union Object;
struct OString;
struct Table;
union Closure;
struct Userdata;
struct Chunk;

struct Value {
private:
    // Later on, if we decide to incorporate NaN-boxing/Pointer-tagging, we can
    // use macros to change what members are to be included in the struct.
    Value_Type m_type = VALUE_NIL;
    union {
        Integer m_integer = 0;
        Number  m_number;
        bool    m_boolean;
        Object *m_object;
        void   *m_pointer; // light userdata.
    };

public:
    static constexpr Value
    make_boolean(bool b)
    {
        Value v{};
        v.m_type    = VALUE_BOOLEAN;
        v.m_boolean = b;
        return v;
    }

    // @note 2025-07-21 Affected by Nan-boxing/Pointer-tagging
    static constexpr Value
    make_number(Number n)
    {
        Value v{};
        v.m_type   = VALUE_NUMBER;
        v.m_number = n;
        return v;
    }

    /** @brief Internal use only. Helps store integers without `lulu_Number`.
     */
    static constexpr Value
    make_integer(Integer i)
    {
        Value v{};
        v.m_type    = VALUE_INTEGER;
        v.m_integer = i;
        return v;
    }

    static Value
    make_object(Object *o, Value_Type t)
    {
        Value v;
        v.m_type   = t;
        v.m_object = o;
        return v;
    }

    static Value
    make_string(OString *s)
    {
        return make_object(reinterpret_cast<Object *>(s), VALUE_STRING);
    }

    static Value
    make_table(Table *t)
    {
        return make_object(reinterpret_cast<Object *>(t), VALUE_TABLE);
    }

    static Value
    make_function(Closure *f)
    {
        return make_object(reinterpret_cast<Object *>(f), VALUE_FUNCTION);
    }

    static Value
    make_lightuserdata(void *p)
    {
        Value v;
        v.m_type    = VALUE_LIGHTUSERDATA;
        v.m_pointer = p;
        return v;
    }

    void
    set_boolean(bool b) noexcept
    {
        *this = this->make_boolean(b);
    }

    void
    set_number(Number d) noexcept
    {
        *this = this->make_number(d);
    }

    void
    set_integer(Integer i) noexcept
    {
        *this = this->make_integer(i);
    }

    void
    set_string(OString *os) noexcept
    {
        *this = this->make_string(os);
    }

    void
    set_table(Table *t) noexcept
    {
        *this = this->make_table(t);
    }

    void
    set_function(Closure *f) noexcept
    {
        *this = this->make_function(f);
    }

    void
    set_lightuserdata(void *p) noexcept
    {
        *this = this->make_lightuserdata(p);
    }

    bool
    operator==(Value other) const;


    //=== VALUE TYPE INFORMATION =========================================== {{{

    static const char *const type_names[VALUE_TYPE_COUNT];

    const char *
    type_name() const
    {
        Value_Type t = this->type();
        return Value::type_names[t];
    }

    // @note 2025-07-19 Affected by Nan-boxing/Pointer-tagging
    constexpr Value_Type
    type() const noexcept
    {
        return this->m_type;
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
    is_integer() const noexcept
    {
        return this->type() == VALUE_INTEGER;
    }

    constexpr bool
    is_lightuserdata() const noexcept
    {
        return this->type() == VALUE_LIGHTUSERDATA;
    }

    constexpr bool
    is_object() const noexcept
    {
        return VALUE_STRING <= this->type() && this->type() <= VALUE_UPVALUE;
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

    constexpr bool
    is_userdata() const noexcept
    {
        return this->type() == VALUE_USERDATA;
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

    /** @note(2025-07-19) Affected by Nan-boxing/pointer-tagging. */
    Integer
    to_integer() const
    {
        lulu_assert(this->is_integer());
        return this->m_integer;
    }

    // @note 2025-07-19 Affected by Nan-boxing/Pointer-tagging
    void *
    to_lightuserdata() const
    {
        lulu_assert(this->is_lightuserdata());
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

    inline Userdata *
    to_userdata() const;

    inline void *
    to_pointer() const;
};

Value
Object_Header::to_value()
{
    Object *o = reinterpret_cast<Object *>(this);
    return Value::make_object(o, this->type);
}

// In C++17, `constexpr inline` guarantees that this variable has the
// same address in all translation units.
constexpr inline Value nil{};

void
value_print(Value v);
