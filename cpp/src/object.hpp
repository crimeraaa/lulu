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
    Object_Header base;
    OString  ostring;
    Table    table;
    Chunk    chunk;
    Closure  function;
    Userdata userdata;
    Upvalue  upvalue;

    Value_Type
    type() const noexcept
    {
        return this->base.type;
    }

    const char *
    type_name() const noexcept
    {
        return Value::type_names[this->type()];
    }

    // We don't need to return a reference because once an object is created
    // its 'next' link remains constant unless it is collected, in which case
    // the GC handles the links for us anyway.
    Object *
    next() noexcept
    {
        return this->base.next;
    }
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

inline Userdata *
Value::to_userdata() const
{
    lulu_assert(this->is_userdata());
    return &this->to_object()->userdata;
}

inline void *
Value::to_pointer() const
{
    switch (this->type()) {
    case VALUE_LIGHTUSERDATA:
        return this->to_lightuserdata();
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
object_free(lulu_VM *L, Object *o);

#ifdef LULU_DEBUG_LOG_GC

// https://gist.github.com/fnky/458719343aabd01cfb17a3a4f7296797
#define ANSI_ESC            "\x1b"
#define ANSI_CSI            ANSI_ESC "["
#define ANSI_FG_RED         31
#define ANSI_FG_GREEN       32
#define ANSI_FG_DEFAULT     39
#define ANSI_COLOR(color)   ANSI_CSI TOKEN_STRINGIFY(color) "m"

// Text is always on the foreground, so colorize that.
#define ANSI_COLOR_TEXT(color, text) \
    ANSI_COLOR(color) text ANSI_COLOR(ANSI_FG_DEFAULT)
#define ANSI_TEXT_RED(text)     ANSI_COLOR_TEXT(ANSI_FG_RED, text)
#define ANSI_TEXT_GREEN(text)   ANSI_COLOR_TEXT(ANSI_FG_GREEN, text)

void
object_gc_print(Object *o, const char *fmt, ...);

#endif // LULU_DEBUG_LOG_GC

/** @brief Allocates an appropriately-sized object of type T and
 *  zero-initializes it.
 *
 * @note(2025-08-27)
 *      Analogous to `object.c:allocateObject()` in Crafting Interpreters 19.3:
 *      Strings.
 */
template<class T>
inline T *
object_new(lulu_VM *L, Object_List **list, Value_Type type, isize extra = 0)
{
    T *o = mem_new<T>(L, extra);
    // Not safe nor intuitive to zero-init flexible-arrays with `*o = {}`
    memset(o, 0, size_of(*o) + extra);

    o->type = type;

    // lgc.c:luaC_link() marks all new objects as black, should we?
    o->set_white();

    // Chain the new object.
    o->next = *list;
    *list   = o->to_object();

#ifdef LULU_DEBUG_LOG_GC
    if (type != VALUE_STRING) {
        object_gc_print(o->to_object(), "[NEW] %zu bytes", sizeof(T) + extra);
    }
#endif
    return o;
}
