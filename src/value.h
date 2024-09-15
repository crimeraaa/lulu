#ifndef LULU_VALUE_H
#define LULU_VALUE_H

#include "lulu.h"
#include "memory.h"

#include <math.h>

typedef double lulu_Number;

#define LULU_NUMBER_FMT             "%.14g"
#define lulu_Number_add(lhs, rhs)   ((lhs) + (rhs))
#define lulu_Number_sub(lhs, rhs)   ((lhs) - (rhs))
#define lulu_Number_mul(lhs, rhs)   ((lhs) * (rhs))
#define lulu_Number_div(lhs, rhs)   ((lhs) / (rhs))
#define lulu_Number_mod(lhs, rhs)   fmod((lhs), (rhs))
#define lulu_Number_pow(lhs, rhs)   pow((lhs), (rhs))
#define lulu_Number_unm(rhs)        (-(rhs))
#define lulu_Number_eq(lhs, rhs)    ((lhs) == (rhs))
#define lulu_Number_lt(lhs, rhs)    ((lhs) < (rhs))
#define lulu_Number_leq(lhs, rhs)   ((lhs) <= (rhs))

typedef enum {
    LULU_TYPE_NIL,
    LULU_TYPE_BOOLEAN,
    LULU_TYPE_NUMBER,
    LULU_TYPE_STRING,
} lulu_Value_Type;

#define LULU_TYPE_COUNT     (LULU_TYPE_STRING + 1)

extern const cstring
LULU_TYPENAMES[LULU_TYPE_COUNT];

typedef struct lulu_Object lulu_Object;
typedef struct lulu_String lulu_String;
typedef struct {
    lulu_Value_Type type;
    union {
        bool         boolean;
        lulu_Number  number;
        lulu_Object *object;
        lulu_String *string;
    }; // Anonymous unions (C11) bring members into the enclosing scope.
} lulu_Value;

typedef struct {
    lulu_Value *values;
    isize       len;
    isize       cap;
} lulu_Value_Array;

#define lulu_Value_typename(value)      LULU_TYPENAMES[(value)->type]

#define lulu_Value_is_nil(value)        ((value)->type == LULU_TYPE_NIL)
#define lulu_Value_is_boolean(value)    ((value)->type == LULU_TYPE_BOOLEAN)
#define lulu_Value_is_number(value)     ((value)->type == LULU_TYPE_NUMBER)
#define lulu_Value_is_object(value)     ((value)->type >= LULU_TYPE_STRING)
#define lulu_Value_is_string(value)     ((value)->type == LULU_TYPE_STRING)

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

/**
 * @note 2024-09-15
 *      Can't make this inline because we need full definition of lulu_Object,
 *      which in turn would cause the headers to recursively include.
 */
void
lulu_Value_set_object(lulu_Value *dst, lulu_Object *object);

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
