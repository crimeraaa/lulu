#ifndef LULU_H
#define LULU_H

#include <stddef.h>
#include <stdint.h>

/**
 * In C++, 'bool' is keyword so including this header (if it exists) is redundant.
 */
#ifndef __cplusplus
#include <stdbool.h>
#endif // __cplusplus

///=== CONFIGURATION ========================================================{{{

/**
 * @brief
 *      The default alignment.
 *
 * @note
 *      x86-64, for example, normally has a 16-byte aligment due to the presence
 *      of long doubles (80-bits). However, because they're only 80 bits, using
 *      them in practice causes them to be padded to 128-bits.
 */
union lulu_User_Alignment {
    void  *p;
    double f;
    long   li;
    long double lf;
};

#define LULU_USER_ALIGNMENT sizeof(union lulu_User_Alignment)

// Determine the language-specific error handling we should use.
#if defined __cplusplus

    // C++11
    #if __cplusplus >= 201103L
        #define LULU_ATTR_NORETURN      [[noreturn]]
        #define LULU_ATTR_DEPRECATED    [[deprecated]]
    #endif
    // C++17
    #if __cplusplus >= 201703L
        #define LULU_ATTR_UNUSED [[maybe_unused]]
    #endif

    #define LULU_TRY(handler)   try
    #define LULU_CATCH(handler)                                                \
        catch(lulu_Status status) {                                            \
            if ((handler)->status == LULU_OK) {                                \
                (handler)->status = status;                                    \
            }                                                                  \
        }
    #define LULU_THROW(handler, status) throw(status)

    typedef int lulu_Jump_Buffer; // Dummy, we don't need this for anything.

#else // __cplusplus not defined.

    #include <setjmp.h>
    #if defined __STDC__ && __STDC_VERSION__ >= 201112L
        #include <stdnoreturn.h>
        #define LULU_ATTR_NORETURN      noreturn
    #endif

    #define LULU_TRY(handler)           if (setjmp((handler)->buffer) == 0)
    #define LULU_CATCH(handler)         else
    #define LULU_THROW(handler, status) longjmp((handler)->buffer, 1)

    typedef jmp_buf lulu_Jump_Buffer;

#endif // __cplusplus

///=== COMPILER-SPECIFIC EXTENSIONS ========================================={{{

// @note 2024-09-22: This is the only 'required' attribute.
#if !defined LULU_ATTR_NORETURN
    #if defined __GNUC__
        #define LULU_ATTR_NORETURN  __attribute__((__noreturn__))
    #elif defined _MSC_VER
        // @warning: Untested!
        #define LULU_ATTR_NORETURN  __declspec(noreturn)
    #else
        #error Please define 'LULU_ATTR_NORETURN' a compiler-specific attribute.
    #endif
#endif

#if !defined LULU_ATTR_DEPRECATED
    #if defined __GNUC__
        #define LULU_ATTR_DEPRECATED    __attribute__((__deprecated__))
    #elif defined _MSC_VER
        #define LULU_ATTR_DEPRECATED    __declspec(deprecated)
    #else
        #define LULU_ATTR_DEPRECATED
    #endif
#endif

#if !defined LULU_ATTR_UNUSED
    #if defined __GNUC__
        #define LULU_ATTR_UNUSED        __attribute__((__unused__))
    #else
        #define LULU_ATTR_UNUSED
    #endif
#endif

// printf validation is nice to have for sanity checking, but not required.
#if defined(__GNUC__)
    #define LULU_ATTR_PRINTF(fmt, args) __attribute__(( __format__ (__printf__, fmt, args) ))
#else
    #define LULU_ATTR_PRINTF(fmt, args)
#endif

/// }}}=========================================================================

/// }}}=========================================================================

/**
 * @note 2024-09-04
 *      Easier to grep.
 */
#define cast(Type)      (Type)
#define unused(Expr)    cast(void)(Expr)
#define size_of(Expr)   cast(isize)(sizeof(Expr))

typedef   uint8_t u8;
typedef  uint16_t u16;
typedef  uint32_t u32;
typedef  uint64_t u64;

typedef    int8_t i8;
typedef   int16_t i16;
typedef   int32_t i32;
typedef   int64_t i64;

typedef        u8 byte;  // Smallest addressable unit.
typedef       u32 byte3; // Use only 24 bits.
typedef    size_t usize;
typedef ptrdiff_t isize;

/**
 * @brief
 *      Although a mere typedef, this indicates intention: C strings are, by
 *      definition, a pointer to a nul terminated sequence of `char`.
 *
 * @note 2024-09-04
 *      Generally, typedef'ing pointers is frowned upon...
 */
typedef const char *cstring;

typedef struct lulu_VM lulu_VM;

typedef void *
(*lulu_Allocator)(void *allocator_data, isize new_size, isize align, void *old_ptr, isize old_size);

typedef enum {
    LULU_OK,
    LULU_ERROR_COMPTIME,
    LULU_ERROR_RUNTIME,
    LULU_ERROR_MEMORY,
} lulu_Status;

#endif // LULU_H
