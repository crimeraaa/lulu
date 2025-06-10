/* This header must be compatible with C. */
#ifndef LULU_H
#define LULU_H

#include <stddef.h>

typedef struct lulu_VM lulu_VM;

typedef void *
(*lulu_Allocator)(void *user_data, void *ptr, size_t old_size, size_t new_size);


#include <math.h>

#define lulu_Number_add(a, b)    ((a) + (b))
#define lulu_Number_sub(a, b)    ((a) - (b))
#define lulu_Number_mul(a, b)    ((a) * (b))
#define lulu_Number_div(a, b)    ((a) / (b))
#define lulu_Number_mod(a, b)    fmod(a, b)
#define lulu_Number_pow(a, b)    pow(a, b)

#endif /* LULU_H */
