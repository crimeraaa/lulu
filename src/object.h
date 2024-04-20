#ifndef LULU_OBJECT_H
#define LULU_OBJECT_H

#include "lulu.h"
#include "limits.h"
#include "memory.h"

typedef lulu_Number Number;
typedef struct Object Object;

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

typedef struct {
    Object object;  // "Inherited" must come first to allow safe type-punning.
    uint32_t hash;  // Hash code for use in addressing a `Table*`.
    int len;        // Nul-terminated length, not including the nul itself.
    char data[];    // See: C99 flexible array members, MUST be last member!
} TString;

typedef struct {
    TValue key;
    TValue value;
} Entry;

typedef struct {
    Entry *entries; // Associative array segment.
    int count;      // Current number of active entries.
    int cap;        // Total number of possible active entries.
} Table;

// NOTE: All `get_*`, `is_*`, `as_*` and `set_*` functions expect a pointer.
#define get_tag(v)          ((v)->tag)
#define get_typename(v)     LULU_TYPENAMES[get_tag(v)]

#define is_nil(v)           (get_tag(v) == TYPE_NIL)
#define is_boolean(v)       (get_tag(v) == TYPE_BOOLEAN)
#define is_number(v)        (get_tag(v) == TYPE_NUMBER)
#define is_object(v)        (get_tag(v) >= TYPE_STRING)
#define is_string(v)        (get_tag(v) == TYPE_STRING)

#define as_boolean(v)       ((v)->as.boolean)
#define as_number(v)        ((v)->as.number)
#define as_object(v)        ((v)->as.object)
#define as_string(v)        cast(TString*, as_object(v))
#define as_cstring(v)       (as_string(v)->data)

#define make_nil()          (TValue){TYPE_NIL,     {.number  = 0}}
#define make_boolean(b)     (TValue){TYPE_BOOLEAN, {.boolean = (b)}}
#define make_number(n)      (TValue){TYPE_NUMBER,  {.number  = (n)}}
#define make_object(p, tt)  (TValue){tt,           {.object  = cast(Object*,p)}}
#define make_string(p)      make_object(p, TYPE_STRING)

// We use a local variable to avoid bugs caused by multiple macro expansion.
// NOTE: We set the value before the tag type in case `val` evaluates `dst`.
#define set_value(tt, as_fn, dst, val) {                                       \
    TValue *_dst  = (dst);                                                     \
    as_fn(_dst)   = (val);                                                     \
    get_tag(_dst) = (tt);                                                      \
}

#define set_nil(v)          set_value(TYPE_NIL,     as_number,  v, 0)
#define set_boolean(v, b)   set_value(TYPE_BOOLEAN, as_boolean, v, b)
#define set_number(v, n)    set_value(TYPE_NUMBER,  as_number,  v, n)
#define set_object(T, v, o) set_value(T,            as_object,  v, o)
#define set_string(v, s)    set_object(TYPE_STRING, v, s)

#define is_falsy(v)         (is_nil(v) || (is_boolean(v) && !as_boolean(v)))

// Writes string representation of `self` to C `stdout`.
void print_value(const TValue *self);

// We cannot use `memcmp` due to struct padding.
bool values_equal(const TValue *lhs, const TValue *rhs);

void init_tarray(TArray *self);
void free_tarray(TArray *self, Alloc *alloc);
void write_tarray(TArray *self, const TValue *value, Alloc *alloc);

// Global functions that deal with strings need the VM to check for interned.
TString *copy_string(VM *vm, const char *literal, int len);

// Assumes all arguments we already verified to be `TString*`.
TString *concat_strings(VM *vm, int argc, const TValue argv[], int len);

void init_table(Table *self);
void free_table(Table *self, Alloc *alloc);
void dump_table(const Table *self, const char *name);
bool get_table(Table *self, const TValue *key, TValue *out);
bool set_table(Table *self, const TValue *key, const TValue *value, Alloc *alloc);

// Place a tombstone value. Analogous to `deleteTable()` in the book.
bool unset_table(Table *self, const TValue *key);

// Analogous to `tableAddAll()` in the book.
void copy_table(Table *dst, const Table *src, Alloc *alloc);

// Mutates the `vm->strings` table. Maps strings to non-nil values.
void set_interned(VM *vm, const TString *key);

// Searches for interned strings. Analogous to `tableFindString()` in the book.
TString *find_interned(VM *vm, const char *data, int len, uint32_t hash);

#endif /* LULU_OBJECT_H */
