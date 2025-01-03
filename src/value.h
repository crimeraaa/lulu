#ifndef LULU_VALUE_H
#define LULU_VALUE_H

#include "memory.h"

#include <math.h>

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

typedef lulu_Value_Type Value_Type;

extern const cstring
LULU_TYPENAMES[LULU_TYPE_COUNT];

typedef lulu_Number    Number;
typedef struct Object  Object;
typedef struct OString OString; // The string object, different from 'LString'.
typedef struct Table   Table;

typedef struct {
    Value_Type type;
    union {
        bool     boolean;
        Number   number;
        OString *string;
        Table   *table;
    }; // Anonymous unions (C11) bring members into the enclosing scope.
} Value;

typedef struct {
    Value *values;
    int    len;
    int    cap;
} VArray;

static inline cstring
value_typename(const Value *value)
{
    return LULU_TYPENAMES[value->type];
}

static inline bool
value_is_nil(const Value *value)
{
    return value->type == LULU_TYPE_NIL;
}

static inline bool
value_is_boolean(const Value *value)
{
    return value->type == LULU_TYPE_BOOLEAN;
}

static inline bool
value_is_number(const Value *value)
{
    return value->type == LULU_TYPE_NUMBER;
}

static inline bool
value_is_string(const Value *value)
{
    return value->type == LULU_TYPE_STRING;
}

static inline bool
value_is_table(const Value *value)
{
    return value->type == LULU_TYPE_TABLE;
}

extern const Value
LULU_VALUE_NIL,
LULU_VALUE_TRUE,
LULU_VALUE_FALSE;

static inline bool
value_is_falsy(const Value *value)
{
    return value_is_nil(value) || (value_is_boolean(value) && !value->boolean);
}

bool
number_to_integer(Number number, int *out_integer);

/**
 * @note 2024-12-28:
 *      Direct floating point to integer conversion are computationally expensive!
 */
bool
value_number_is_integer(const Value *value, int *out_integer);

static inline void
value_set_nil(Value *dst)
{
    dst->type   = LULU_TYPE_NIL;
    dst->number = 0;
}

static inline void
value_set_boolean(Value *dst, bool b)
{
    dst->type    = LULU_TYPE_BOOLEAN;
    dst->boolean = b;
}

static inline void
value_set_number(Value *dst, Number n)
{
    dst->type   = LULU_TYPE_NUMBER;
    dst->number = n;
}

static inline void
value_set_string(Value *dst, OString *string)
{
    dst->type   = LULU_TYPE_STRING;
    dst->string = string;
}

static inline void
value_set_table(Value *dst, Table *table)
{
    dst->type   = LULU_TYPE_TABLE;
    dst->table  = table;
}

void
value_print(const Value *value);

bool
value_eq(const Value *a, const Value *b);

void
varray_init(VArray *self);

void
varray_append(lulu_VM *vm, VArray *self, const Value *value);

void
varray_write_at(lulu_VM *vm, VArray *self, int index, const Value *value);

void
varray_free(lulu_VM *vm, VArray *self);

// Sets cap only.
void
varray_reserve(lulu_VM *vm, VArray *self, int new_cap);

#endif // LULU_VALUE_H
