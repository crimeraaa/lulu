#ifndef LULU_H
#define LULU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @note 2024-09-04
 *      Easier to grep.
 */
#define cast(Type)              (Type)
#define unused(Expr)            cast(void)(Expr)
#define size_of(Expr)           cast(isize)(sizeof(Expr))

typedef   uint8_t u8;
typedef  uint16_t u16;
typedef  uint32_t u32;
typedef  uint64_t u64;

typedef    int8_t i8;
typedef   int16_t i16;
typedef   int32_t i32;
typedef   int64_t i64;

typedef        u8 byte;  // Smallest addressable unit.
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

/**
 * @brief
 *      A read-only view into some characters.
 * 
 * @note 2024-09-04
 *      The underlying buffer may not necessarily be nul terminated!
 */
typedef struct {
    const char *data;
    isize       len;
} String;

/**
 * @brief
 *      Construct a String from a C-string literal. This is designed to work for
 *      both post-declaration assignment.
 * 
 * @warning 2024-09-07
 *      C99-style compound literals have very different semantics in C++.
 *      "Struct literals" are valid (in C++) due to implicit copy constructors.
 */
#ifdef __cplusplus
#define String_literal(cstr) {(cstr), size_of(cstr) - 1}
#else // !__cplusplus
#define String_literal(cstr) cast(String){(cstr), size_of(cstr) - 1}
#endif // __cplusplus

typedef struct lulu_VM lulu_VM;

#endif // LULU_H
