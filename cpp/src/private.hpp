#pragma once

#include <inttypes.h> // PRI* macros
#include <stddef.h>   // size_t
#include <stdint.h>   // [u]int*_t

#include "lulu.h"
#include "snippets.hpp"

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using i8  = int8_t;
using i32 = int32_t;


/** @brief Primary size type.
 *
 * @details
 *  Theoretically, this is not enough to represent the full address space.
 *  However in practice most of the address space is invalid anyway.
 *
 *  E.g. on a 64 bit machine, a platform may only use 48 bits per address, so
 *  signed 64 bit sizes are overkill as it will be impossible to commit even 1
 *  quadrillion bytes (~50 bits) of memory. So we assume that this type is more
 *  than adequate for our purposes.
 *
 *  Of course, the 32-bit targets are a different story...
 */
using isize = ptrdiff_t;

/** @brief Only used for consistency with C standard library functions and
 *  allocation functions. Prefer `isize` otherwise.
 */
using usize = size_t;

#define ISIZE_WIDTH     PTRDIFF_WIDTH
#define ISIZE_FMT       "ti"
#define USIZE_MAX       (~usize(0) - 2)
#define unused(expr)    (void)(expr)
#define size_of(expr)   isize(sizeof(expr))
#define count_of(array) isize(sizeof(array) / sizeof((array)[0]))

#define BIT_FLAG(n)  (1 << (n))

enum Object_Mark_Flag : u8 {
    // 0b0000_0001
    // Object has not yet been processed by the current garbage collector run.
    OBJECT_WHITE = BIT_FLAG(0),

    // 0b0000_0010
    // Object has been traversed; all its children have been checked.
    OBJECT_BLACK = BIT_FLAG(1),

    // 0b0000_0100
    // Object is never collectible no matter what.
    OBJECT_FIXED = BIT_FLAG(2),
};


using Type    = lulu_Type;
using Number  = lulu_Number;
using Integer = lulu_Integer;

inline isize
operator""_i(unsigned long long i)
{
    return static_cast<isize>(i);
}

/**
 * @param [out] i
 *      Holds the result of conversion so that the functioon can return
 *      a boolean value to indicate success or failure.
 *
 * @return
 *      true if conversion occured without loss of precision, else false.
 */
inline bool
number_to_integer(Number n, Integer *i)
{
    *i = static_cast<Integer>(n);
    return lulu_Number_eq(static_cast<Number>(*i), n);
}

enum Value_Type : u8 {
    VALUE_NIL           = LULU_TYPE_NIL,
    VALUE_BOOLEAN       = LULU_TYPE_BOOLEAN,
    VALUE_LIGHTUSERDATA = LULU_TYPE_LIGHTUSERDATA,
    VALUE_NUMBER        = LULU_TYPE_NUMBER,
    VALUE_STRING        = LULU_TYPE_STRING,
    VALUE_TABLE         = LULU_TYPE_TABLE,
    VALUE_FUNCTION      = LULU_TYPE_FUNCTION,

    // Not accessible from user code.
    VALUE_CHUNK,
    VALUE_UPVALUE,
    VALUE_INTEGER,
};

// 'slice' the Value_Type enum up to the last user-facing type.
#define VALUE_TYPE_LAST  VALUE_FUNCTION

#ifdef LULU_DEBUG
#   define VALUE_TYPE_COUNT (VALUE_INTEGER + 1)
#else
#   define VALUE_TYPE_COUNT (VALUE_FUNCTION + 1)
#endif

union Object;
using Object_Mark = u8;

// This is mainly for GDB Python pretty printers...
using Object_List = Object;
using GC_List = Object;

// Do not create stack-allocated instances of these; unaligned accesses may occur.
struct [[gnu::packed]] Object_Header {
    Object_List *next;
    Value_Type   type;
    Object_Mark  mark;

    Object *
    to_object() noexcept
    {
        return reinterpret_cast<Object *>(this);
    }

    bool
    is_white() const noexcept
    {
        return this->get<OBJECT_WHITE>();
    }

    bool
    is_black() const noexcept
    {
        return this->get<OBJECT_BLACK>();
    }

    bool
    is_gray() const noexcept
    {
        // Neither white bit nor black bit toggled?
        return !this->is_white() && !this->is_black();
    }


    bool
    is_fixed() const noexcept
    {
        return this->get<OBJECT_FIXED>();
    }

    void
    set_white()
    {
        this->set<OBJECT_WHITE>();
        this->clear<OBJECT_BLACK>();
    }

    void
    set_gray_from_white()
    {
        this->clear<OBJECT_WHITE>();
    }

    void
    set_gray_from_black()
    {
        this->clear<OBJECT_BLACK>();
    }

    void
    set_black()
    {
        this->set<OBJECT_BLACK>();
    }

    void
    set_fixed()
    {
        this->set<OBJECT_FIXED>();
    }

private:
    template<Object_Mark Bit>
    bool
    get() const noexcept
    {
        return this->mark & Bit;
    }

    template<Object_Mark Bit>
    void
    set()
    {
        this->mark |= Bit;
    }

    template<Object_Mark Bit>
    void
    clear()
    {
        this->mark &= ~Bit;
    }
};

template<class T>
inline T
max(T a, T b)
{
    return (a > b) ? a : b;
}

template<class T>
inline void
swap(T *restrict a, T *restrict b)
{
    T tmp = *a;
    *a    = *b;
    *b    = tmp;
}
