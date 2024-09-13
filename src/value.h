#ifndef LULU_VALUE_H
#define LULU_VALUE_H

#include "lulu.h"
#include "memory.h"

#include <math.h>

typedef double lulu_Number;

#define LULU_NUMBER_FMT         "%.14g"
#define lulu_Number_add(x, y)   ((x) + (y))
#define lulu_Number_sub(x, y)   ((x) - (y))
#define lulu_Number_mul(x, y)   ((x) * (y))
#define lulu_Number_div(x, y)   ((x) / (y))
#define lulu_Number_mod(x, y)   fmod((x), (y))
#define lulu_Number_pow(x, y)   pow((x), (y))
#define lulu_Number_unm(x)      (-(x))
#define lulu_Number_eq(x, y)    ((x) == (y))
#define lulu_Number_lt(x, y)    ((x) < (y))
#define lulu_Number_leq(x, y)   ((x) <= (y))

typedef enum {
    LULU_TYPE_NIL,
    LULU_TYPE_BOOLEAN,
    LULU_TYPE_NUMBER,
} lulu_Value_Type;

#define LULU_TYPE_COUNT     (LULU_TYPE_NUMBER + 1)

extern const cstring
LULU_TYPENAMES[LULU_TYPE_COUNT];

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

#define lulu_Value_typename(self)       LULU_TYPENAMES[(self)->type]

#define lulu_Value_is_nil(self)         ((self)->type == LULU_TYPE_NIL)
#define lulu_Value_is_boolean(self)     ((self)->type == LULU_TYPE_BOOLEAN)
#define lulu_Value_is_number(self)      ((self)->type == LULU_TYPE_NUMBER)

extern const lulu_Value LULU_VALUE_NIL;
extern const lulu_Value LULU_VALUE_TRUE;
extern const lulu_Value LULU_VALUE_FALSE;

static inline bool
lulu_Value_is_falsy(const lulu_Value *value)
{
    return lulu_Value_is_nil(value)
        || (lulu_Value_is_boolean(value) && !value->boolean);
}

static inline void
lulu_Value_set_nil(lulu_Value *dst)
{
    dst->type   = LULU_TYPE_NIL;
    dst->number = 0;
}

static inline void
lulu_Value_set_boolean(lulu_Value *dst, bool b)
{
    dst->type    = LULU_TYPE_BOOLEAN;
    dst->boolean = b;
}

static inline void
lulu_Value_set_number(lulu_Value *dst, lulu_Number n)
{
    dst->type   = LULU_TYPE_NUMBER;
    dst->number = n;
}

bool
lulu_Value_eq(const lulu_Value *a, const lulu_Value *b);

void
lulu_Value_Array_init(lulu_Value_Array *self);

void
lulu_Value_Array_write(lulu_VM *vm, lulu_Value_Array *self, const lulu_Value *value);

void
lulu_Value_Array_free(lulu_VM *vm, lulu_Value_Array *self);

void
lulu_Value_Array_reserve(lulu_VM *vm, lulu_Value_Array *self, isize new_cap);

#endif // LULU_VALUE_H
