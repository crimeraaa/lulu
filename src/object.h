#ifndef LULU_OBJECT_H
#define LULU_OBJECT_H

#include "conf.h"

typedef enum {
    TYPE_NIL,
    TYPE_BOOLEAN,
    TYPE_NUMBER,
} VType;

// Tagged union to somewhat enfore safety. Also, anonymous unions are from C11.
typedef struct {
    VType tag;
    union {
        bool boolean;
        double number;
    } as;
} TValue;

typedef struct {
    TValue *values;
    int len;
    int cap;
} TArray;

#define make_nil()      (TValue){.tag = TYPE_NIL,       .as = {.number  = 0}}
#define make_boolean(b) (TValue){.tag = TYPE_BOOLEAN,   .as = {.boolean = (b)}}
#define make_number(n)  (TValue){.tag = TYPE_NUMBER,    .as = {.number  = (n)}}

void print_value(const TValue *self);

void init_tarray(TArray *self);
void free_tarray(TArray *self);
void write_tarray(TArray *self, const TValue *value);

#endif /* LULU_OBJECT_H */
