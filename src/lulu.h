#ifndef LULU_H
#define LULU_H

#include <stddef.h>     // size_t, ptrdiff_t
#include <stdint.h>     // [u]int8_t, [u]int16_t, [u]int32_t, [u]int64_t

// In C++, 'bool' is keyword so including this header (if it exists) is redundant.
#ifndef __cplusplus
#include <stdbool.h>    // bool
#endif

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

#define LULU_IMPL_ERROR_HANDLING_EXCEPTION   1
#define LULU_IMPL_ERROR_HANDLING_LONGJMP     2

// Determine the language-specific error handling we should use.
#if defined(__cplusplus)

    #define LULU_IMPL_ERROR_HANDLING    LULU_IMPL_ERROR_HANDLING_EXCEPTION

    // C++11
    #if __cplusplus >= 201103L
        #define LULU_ATTR_NORETURN      [[noreturn]]
        #define LULU_ATTR_DEPRECATED    [[deprecated]]
    #endif

    // C++17
    #if __cplusplus >= 201703L
        #define LULU_ATTR_UNUSED [[maybe_unused]]
    #endif

    /**
     * @note 2024-09-29
     *      'catch' doesn't do anything, it's only present because C++ requires
     *      all 'try' to have a corresponding 'catch' (and rightfully so!).
     *
     *      We already set 'handler->status' in 'lulu_VM_run_protected()'.
     */
    #define LULU_IMPL_TRY(handler)      try
    #define LULU_IMPL_CATCH(handler)    catch (...)
    #define LULU_IMPL_THROW(handler)    throw (handler)

    typedef int lulu_Jump_Buffer; // Dummy, we don't need this for anything.

#else // __cplusplus not defined.

    #include <setjmp.h>

    #define LULU_IMPL_ERROR_HANDLING    LULU_IMPL_ERROR_HANDLING_LONGJMP

    #if defined(__STDC__) && (__STDC_VERSION__ >= 201112L)
        #include <stdnoreturn.h>
        #define LULU_ATTR_NORETURN      noreturn
    #endif

    #define LULU_IMPL_TRY(handler)      if (setjmp((handler)->buffer) == 0)
    #define LULU_IMPL_CATCH(handler)    else
    #define LULU_IMPL_THROW(handler)    longjmp((handler)->buffer, 1)

    typedef jmp_buf lulu_Jump_Buffer;

#endif // __cplusplus

///=== COMPILER-SPECIFIC EXTENSIONS ========================================={{{

// @note 2024-09-22: This is the only 'required' attribute.
#if !defined LULU_ATTR_NORETURN
    #if defined(__GNUC__)
        #define LULU_ATTR_NORETURN  __attribute__((__noreturn__))
    #elif defined _MSC_VER
        // @warning: Untested!
        #define LULU_ATTR_NORETURN  __declspec(noreturn)
    #else
        #error Please define 'LULU_ATTR_NORETURN' to a compiler-specific attribute.
    #endif
#endif

#if !defined LULU_ATTR_DEPRECATED
    #if defined(__GNUC__)
        #define LULU_ATTR_DEPRECATED    __attribute__((__deprecated__))
    #elif defined _MSC_VER
        #define LULU_ATTR_DEPRECATED    __declspec(deprecated)
    #else
        #define LULU_ATTR_DEPRECATED
    #endif
#endif

#if !defined LULU_ATTR_UNUSED
    #if defined(__GNUC__)
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
typedef double lulu_Number;

typedef void *
(*lulu_Allocator)(void *allocator_data, isize new_size, isize align, void *old_ptr, isize old_size);

typedef enum {
    LULU_OK,
    LULU_ERROR_COMPTIME,
    LULU_ERROR_RUNTIME,
    LULU_ERROR_MEMORY,
} lulu_Status;

/**
 * @brief
 *      Check if the stack can accomodate 'count' extra elements.
 *      Throws a runtime error if we cannot.
 *
 * @todo 2024-09-29
 *      Realloc the stack as needed?
 */
void
lulu_check_stack(lulu_VM *vm, int count);

///=== TYPE QUERY FUNCTIONS ====================================================

cstring
lulu_typename(lulu_VM *vm, int offset);

bool
lulu_is_nil(lulu_VM *vm, int offset);

bool
lulu_is_boolean(lulu_VM *vm, int offset);

bool
lulu_is_number(lulu_VM *vm, int offset);

bool
lulu_is_string(lulu_VM *vm, int offset);

///=============================================================================

///=== STACK MANIPULATION FUNCTIONS ============================================

void
lulu_pop(lulu_VM *vm, int count);

void
lulu_push_nil(lulu_VM *vm, int count);

void
lulu_push_boolean(lulu_VM *vm, bool boolean);

void
lulu_push_number(lulu_VM *vm, lulu_Number number);

void
lulu_push_cstring(lulu_VM *vm, cstring cstr);

void
lulu_push_string(lulu_VM *vm, const char *data, isize len);

void
lulu_push_table(lulu_VM *vm, isize count);

#define lulu_push_empty_table(vm)   lulu_push_table(vm, 0)
#define lulu_push_literal(vm, cstr) lulu_push_string(vm, cstr, size_of(cstr) - 1)

///=============================================================================

#endif // LULU_H
