#ifndef LULU_VALUE_H
#define LULU_VALUE_H

#include "lulu.h"
#include "memory.h"

typedef double lulu_Number;

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
    };
} lulu_Value;

typedef struct {
    lulu_Allocator allocator;
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

void lulu_Value_Array_init(lulu_Value_Array *self, const lulu_Allocator *allocator);
void lulu_Value_Array_write(lulu_Value_Array *self, const lulu_Value *v);
void lulu_Value_Array_free(lulu_Value_Array *self);
void lulu_Value_Array_reserve(lulu_Value_Array *self, isize new_cap);

#endif // LULU_VALUE_H
