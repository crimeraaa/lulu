#ifndef LULU_OBJECT_H
#define LULU_OBJECT_H

#include "lulu.h"

typedef lulu_Number Number;

typedef enum {
    TYPE_NIL,
    TYPE_BOOLEAN,
    TYPE_NUMBER,
} VType;

// Please keep this up to date as needed!
#define NUM_TYPES   (TYPE_NUMBER + 1)

// Lookup table: maps `VType` to `const char*`.
extern const char *const LULU_TYPENAMES[];

// Tagged union to somewhat enfore safety. Also, anonymous unions are from C11.
typedef struct {
    VType tag;
    union {
        bool boolean;
        Number number;
    } as;
} TValue;

typedef struct {
    TValue *values;
    int len;
    int cap;
} TArray;

#define get_tagtype(v)      ((v)->tag)
#define get_typename(v)     LULU_TYPENAMES[get_tagtype(v)]

#define is_nil(v)           (get_tagtype(v) == TYPE_NIL)
#define is_boolean(v)       (get_tagtype(v) == TYPE_BOOLEAN)
#define is_number(v)        (get_tagtype(v) == TYPE_NUMBER)

#define as_boolean(v)       ((v)->as.boolean)
#define as_number(v)        ((v)->as.number)

#define make_nil()          (TValue){TYPE_NIL,       {.number  = 0}}
#define make_boolean(b)     (TValue){TYPE_BOOLEAN,   {.boolean = (b)}}
#define make_number(n)      (TValue){TYPE_NUMBER,    {.number  = (n)}}

#define set_nil(v)          (get_tagtype(v) = TYPE_NIL, as_number(v) = 0)
#define set_boolean(v, b)   (as_boolean(v) = (b))
#define set_number(v, n)    (as_number(v) = (n))

void print_value(const TValue *self);

void init_tarray(TArray *self);
void free_tarray(TArray *self);
void write_tarray(TArray *self, const TValue *value);

#endif /* LULU_OBJECT_H */
