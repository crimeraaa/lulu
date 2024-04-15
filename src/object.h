#ifndef LULU_OBJECT_H
#define LULU_OBJECT_H

#include "lulu.h"
#include "limits.h"

typedef lulu_Number    Number;
typedef struct Object  Object;
typedef struct TString TString;

typedef enum {
    TYPE_NIL,
    TYPE_BOOLEAN,
    TYPE_NUMBER,
    TYPE_STRING,
} VType;

// Please keep this up to date as needed!
#define NUM_TYPES   (TYPE_STRING + 1)

// Lookup table: maps `VType` to `const char*`.
extern const char *const LULU_TYPENAMES[];

// Tagged union to somewhat enfore safety. Also, anonymous unions are from C11.
typedef struct {
    VType tag;
    union {
        bool boolean;
        Number number;
        Object *object; // Some heap-allocated, GC-managed data.
    } as;
} TValue;

typedef struct {
    TValue *values;
    int len;
    int cap;
} TArray;

struct Object {
    VType tag;      // Must be consistent with the parent `TValue`.
    Object *next;   // Intrusive list (linked list) node.
};

struct TString {
    Object object;  // "Inherited" must come first to allow safe type-punning.
    int len;        // Nul-terminated length, not including the nul itself.
    char data[];    // See: C99 flexible array members, MUST be last member!
};

// NOTE: All `get_*`, `is_*`, `as_*` and `set_*` functions expect a pointer.
#define get_tagtype(v)      (v)->tag
#define get_typename(v)     LULU_TYPENAMES[get_tagtype(v)]

#define is_nil(v)           (get_tagtype(v) == TYPE_NIL)
#define is_boolean(v)       (get_tagtype(v) == TYPE_BOOLEAN)
#define is_number(v)        (get_tagtype(v) == TYPE_NUMBER)
#define is_object(v)        (get_tagtype(v) >= TYPE_STRING)
#define is_string(v)        is_objtype(v, TYPE_STRING)

// Use a function to avoid multiple macro expansion.
static inline bool is_objtype(const TValue *self, VType expected) {
    return is_object(self) && get_tagtype(self) == expected;
}

#define as_boolean(v)       (v)->as.boolean
#define as_number(v)        (v)->as.number
#define as_object(v)        (v)->as.object
#define as_string(v)        cast(TString*, as_object(v))
#define as_cstring(v)       as_string(v)->data

#define make_nil()          (TValue){TYPE_NIL,     {.number  = 0}}
#define make_boolean(b)     (TValue){TYPE_BOOLEAN, {.boolean = (b)}}
#define make_number(n)      (TValue){TYPE_NUMBER,  {.number  = (n)}}
#define make_object(p, tt)  (TValue){tt,           {.object  = cast(Object*,p)}}
#define make_string(p)      make_object(p, TYPE_STRING)

/**
 * @note    Setting value BEFORE type tag is needed for evaluating type.
 *          Also, don't use `tag` as the macro parameter name as substitution
 *          will mess up in that case.
 */
#define set_tagtype(v, tt)  (get_tagtype(v) = (tt))
#define set_nil(v)          (as_number(v)   = 0,    set_tagtype(v, TYPE_NIL))
#define set_boolean(v, b)   (as_boolean(v)  = (b),  set_tagtype(v, TYPE_BOOLEAN))
#define set_number(v, n)    (as_number(v)   = (n),  set_tagtype(v, TYPE_NUMBER))
#define set_string(v, s)    (as_string(v)   = (s),  set_tagtype(v, TYPE_STRING))

#define is_falsy(v)         (is_nil(v) || (is_boolean(v) && !as_boolean(v)))

// Writes to C `stdout`.
void print_value(const TValue *self);

// We cannot use `memcmp` due to struct padding.
bool values_equal(const TValue *lhs, const TValue *rhs);

void init_tarray(TArray *self);
void free_tarray(TArray *self);
void write_tarray(TArray *self, const TValue *value);

TString *copy_string(VM *vm, const char *literal, int len);
TString *concat_strings(VM *vm, const TString *lhs, const TString *rhs);

#endif /* LULU_OBJECT_H */
