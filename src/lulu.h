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

typedef void *(*lulu_Allocator)(void *allocator_data, isize new_size, isize align, void *old_ptr, isize old_size);

typedef enum {
    LULU_OK,
    LULU_ERROR_COMPTIME,
    LULU_ERROR_RUNTIME,
    LULU_ERROR_MEMORY,
} lulu_Status;

#endif // LULU_H
