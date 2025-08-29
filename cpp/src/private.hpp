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


/**
 * @brief
 *      Theoretically, this is not enough to represent the full address
 *      space. However in practice most of the address space is invalid
 *      anyway.
 *
 *      E.g. on a 64 bit machine, a platform may only use 48 bits per
 *      address, so signed 64 bit sizes are overkill as it will be
 *      impossible to commit even 1 quadrillion bytes (~50 bits) of memory.
 *      So we assume that this type is more than adequate for our purposes.
 */
using isize = ptrdiff_t;

/**
 * @brief
 *      Only used for consistency with C standard library functions and
 *      allocation functions. Prefer `isize` otherwise.
 */
using usize = size_t;

#define ISIZE_WIDTH     PTRDIFF_WIDTH
#define ISIZE_FMT       "ti"
#define unused(expr)    (void)(expr)
#define size_of(expr)   isize(sizeof(expr))
#define count_of(array) isize(sizeof(array) / sizeof((array)[0]))

#ifndef restrict
#    if defined(__GNUC__) || defined(__clang__)
#        define restrict __restrict__
#    elif defined(_MSC_VER) // ^^^ __GNUC__ || __clang___, vvv _MSC_VER
#        define restrict __restrict
#    else // ^^^ _MSC_VER, vvv else
#        define restrict
#    endif
#endif // restrict

#define BITFLAG(n)  (1 << (n))

enum Object_Mark_Flag : u8 {
    // 0b0000_0000
    // Object has not yet been processed by the current garbage collector run.
    OBJECT_WHITE = 0,

    // 0b0000_0001
    OBJECT_GRAY = 1,

    // 0b0000_0010
    // Object has been traversed; all its children have been checked.
    OBJECT_BLACK = 2,

    OBJECT_COLOR_MASK = OBJECT_GRAY | OBJECT_BLACK,

    // 0b0000_0100
    // Object is never collectible no matter what.
    OBJECT_FIXED = BITFLAG(2),
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

#define VALUE_TYPE_LAST  VALUE_FUNCTION
#define VALUE_TYPE_COUNT VALUE_INTEGER + 1

union Object;
using Object_Mark = u8;

// Do not create stack-allocated instances of these; unaligned accesses may occur.
struct [[gnu::packed]] Object_Header {
    Object     *next;
    Value_Type  type;
    Object_Mark mark;

    Object *
    to_object() noexcept
    {
        return reinterpret_cast<Object *>(this);
    }

    bool
    is_white() const noexcept
    {
        return (this->mark & OBJECT_COLOR_MASK) == OBJECT_WHITE;
    }

    bool
    is_gray() const noexcept
    {
        return this->mark & OBJECT_GRAY;
    }

    bool
    is_black() const noexcept
    {
        return this->mark & OBJECT_BLACK;
    }


    bool
    is_fixed() const noexcept
    {
        return this->mark & OBJECT_FIXED;
    }

    void
    set_white()
    {
        // Clears gray and black bits
        this->mark &= ~Object_Mark(OBJECT_COLOR_MASK);
    }

    void
    set_gray()
    {
        this->mark |= OBJECT_GRAY;
    }

    void
    set_black()
    {
        this->mark |= OBJECT_BLACK;
    }

    void
    set_fixed()
    {
        this->mark |= OBJECT_FIXED;
    }

    void
    clear_fixed()
    {
        this->mark &= ~Object_Mark(OBJECT_FIXED);
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
