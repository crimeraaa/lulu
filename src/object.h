#ifndef LULU_OBJECT_H
#define LULU_OBJECT_H

#include "lulu.h"
#include "limits.h"

typedef struct Object Object;
typedef struct Alloc  Alloc;

typedef enum {
    TYPE_NIL,
    TYPE_BOOLEAN,
    TYPE_NUMBER,
    TYPE_STRING,
    TYPE_TABLE,
} VType;

// Please keep this up to date as needed!
#define NUM_TYPES   (TYPE_TABLE + 1)

// Lookup table: maps `VType` to `const char*`.
extern const char *const LULU_TYPENAMES[];

typedef struct Value {
    VType tag;
    union {
        bool    boolean;
        Number  number;
        Object *object; // Some heap-allocated, GC-managed data.
    } as;
} Value;

struct Object {
    Object  *next; // Intrusive list (linked list) node.
    VType    tag;  // Must be consistent with the parent `Value`.
};

typedef struct String {
    Object   object; // "Inherited" must come first to allow safe type-punning.
    uint32_t hash;   // Used when strings are used as table keys.
    int      len;    // String length with nul and escapes omitted.
    char     data[]; // See: C99 flexible array members, MUST be last member!
} String;

typedef struct {
    Value key;
    Value value;
} Entry;

typedef struct {
    Value *values;
    int    len;
    int    cap;
} VArray ;

typedef struct {
    Object object;  // For user-facing tables, not by VM internal tables.
    Entry *entries; // Associative array segment.
    int    count;   // Current number of active entries in the entries array.
    int    cap;     // Total number of entries the entries array can hold.
} Table;

// NOTE: All `get_*`, `is_*`, `as_*` and `set_*` functions expect a pointer.
#define get_tag(v)          ((v)->tag)
#define get_typename(v)     LULU_TYPENAMES[get_tag(v)]

#define is_nil(v)           (get_tag(v) == TYPE_NIL)
#define is_boolean(v)       (get_tag(v) == TYPE_BOOLEAN)
#define is_number(v)        (get_tag(v) == TYPE_NUMBER)
#define is_object(v)        (get_tag(v) >= TYPE_STRING)
#define is_string(v)        (get_tag(v) == TYPE_STRING)
#define is_table(v)         (get_tag(v) == TYPE_TABLE)

#define as_boolean(v)       ((v)->as.boolean)
#define as_number(v)        ((v)->as.number)
#define as_object(v)        ((v)->as.object)
#define as_pointer(v)       cast(void*,   as_object(v))
#define as_string(v)        cast(String*, as_object(v))
#define as_table(v)         cast(Table*,  as_object(v))
#define as_cstring(v)       (as_string(v)->data)

#define make_nil()          (Value){TYPE_NIL,     {.number  = 0}}
#define make_boolean(b)     (Value){TYPE_BOOLEAN, {.boolean = (b)}}
#define make_number(n)      (Value){TYPE_NUMBER,  {.number  = (n)}}
#define make_object(p, tt)  (Value){tt,           {.object  = cast(Object*,p)}}
#define make_string(p)      make_object(p, TYPE_STRING)
#define make_table(p)       make_object(p, TYPE_TABLE)

// We use a local variable to avoid bugs caused by multiple macro expansion.
// NOTE: We set the value before the tag type in case `val` evaluates `dst`.
#define setv_value(tt, as_fn, dst, val) {                                      \
    Value *_dst   = (dst);                                                     \
    as_fn(_dst)   = (val);                                                     \
    get_tag(_dst) = (tt);                                                      \
}

#define setv_nil(v)             setv_value(TYPE_NIL,     as_number,  v, 0)
#define setv_boolean(v, b)      setv_value(TYPE_BOOLEAN, as_boolean, v, b)
#define setv_number(v, n)       setv_value(TYPE_NUMBER,  as_number,  v, n)
#define setv_object(T, v, o)    setv_value(T, as_object,  v, cast(Object*, o))
#define setv_string(dst, src)   setv_object(TYPE_STRING, dst, src)
#define setv_table(dst, src)    setv_object(TYPE_TABLE,  dst, src)

#define is_falsy(v)         (is_nil(v) || (is_boolean(v) && !as_boolean(v)))

// Writes string representation of `self` to C `stdout`.
void print_value(const Value *self, bool isdebug);

// See: https://www.lua.org/source/5.1/lvm.c.html#luaV_tonumber
// Note that this will likely mutate `self`!
const Value *value_tonumber(Value *self);

// Assumes buffer is a fixed-size array of length `MAX_TOSTRING`.
// If `out` is not `NULL`, it will be set to -1 if we do not own the result.
const char *value_tocstring(const Value *self, char *buffer, int *out);

// We cannot use `memcmp` due to struct padding.
bool values_equal(const Value *lhs, const Value *rhs);

void init_varray(VArray *self);
void free_varray(VArray *self, Alloc *alloc);
void write_varray(VArray *self, const Value *value, Alloc *alloc);

// Mutates the `vm->strings` table. Maps strings to non-nil values.
void set_interned(VM *vm, const String *key);

// Searches for interned strings. Analogous to `tableFindString()` in the book.
String *find_interned(VM *vm, StrView view, uint32_t hash);

#endif /* LULU_OBJECT_H */
