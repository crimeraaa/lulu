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

typedef struct lulu_VM lulu_VM;

#endif // LULU_H
