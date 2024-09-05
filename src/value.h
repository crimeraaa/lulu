#ifndef LULU_VALUE_H
#define LULU_VALUE_H

#include "lulu.h"
#include "memory.h"

#include <math.h>

typedef double lulu_Number;

#define lulu_Number_add(x, y)   ((x) + (y))
#define lulu_Number_sub(x, y)   ((x) - (y))
#define lulu_Number_mul(x, y)   ((x) * (y))
#define lulu_Number_div(x, y)   ((x) / (y))
#define lulu_Number_mod(x, y)   fmod((x), (y))
#define lulu_Number_pow(x, y)   pow((x), (y))

typedef enum {
    LULU_VALUE_TYPE_NIL,
    LULU_VALUE_TYPE_BOOLEAN,
    LULU_VALUE_TYPE_NUMBER,
} lulu_Value_Type;

typedef struct {
    lulu_Value_Type type;
    union {
        bool        boolean;
        lulu_Number number;
    }; // Anonymous unions (C11) bring members into the enclosing scope.
} lulu_Value;

typedef struct {
    lulu_Value *values;
    isize       len;
    isize       cap;
} lulu_Value_Array;

#define lulu_Value_set_nil(self)                                               \
do {                                                                           \
    lulu_Value *_dst = (self);                                                 \
    _dst->type       = LULU_VALUE_TYPE_NIL;                                    \
    _dst->number     = 0;                                                      \
} while (0)

#define lulu_Value_set_boolean(self, b)                                        \
do {                                                                           \
    lulu_Value *_dst = (self);                                                 \
    _dst->type       = LULU_VALUE_TYPE_BOOLEAN;                                \
    _dst->boolean    = (b);                                                    \
} while (0)

#define lulu_Value_set_number(self, n)                                         \
do {                                                                           \
    lulu_Value *_dst = (self);                                                 \
    _dst->type       = LULU_VALUE_TYPE_NUMBER;                                 \
    _dst->number     = (n);                                                    \
} while (0)

void lulu_Value_Array_init(lulu_Value_Array *self);
void lulu_Value_Array_write(lulu_VM *vm, lulu_Value_Array *self, const lulu_Value *v);
void lulu_Value_Array_free(lulu_VM *vm, lulu_Value_Array *self);
void lulu_Value_Array_reserve(lulu_VM *vm, lulu_Value_Array *self, isize new_cap);

#endif // LULU_VALUE_H
