#ifndef LULU_OBJECT_H
#define LULU_OBJECT_H

#include "lulu.h"
#include "limits.h"
#include "memory.h"

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

struct Value {
    VType tag;
    union {
        bool    boolean;
        Number  number;
        Object *object; // Some heap-allocated, GC-managed data.
    } as;
};

struct Object {
    Object  *next; // Intrusive list (linked list) node.
    VType    tag;  // Must be consistent with the parent `Value`.
};

struct String {
    Object   object; // "Inherited" must come first to allow safe type-punning.
    uint32_t hash;   // Used when strings are used as table keys.
    int      len;    // String length with nul and escapes omitted.
    char     data[]; // See: C99 flexible array members, MUST be last member!
};

struct Entry {
    Value key;
    Value value;
};

struct VArray {
    Value *values;
    int    len;
    int    cap;
};

struct Table {
    Object object;    // For user-facing tables, not by VM internal tables.
    Entry *hashmap;   // Associative array segment.
    int    hashcount; // Current number of active entries in the hashmap.
    int    hashcap;   // Total number of entries the hashmap can hold.
};

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
#define set_value(tt, as_fn, dst, val) {                                       \
    Value *_dst  = (dst);                                                     \
    as_fn(_dst)   = (val);                                                     \
    get_tag(_dst) = (tt);                                                      \
}

#define setv_nil(v)          set_value(TYPE_NIL,     as_number,  v, 0)
#define setv_boolean(v, b)   set_value(TYPE_BOOLEAN, as_boolean, v, b)
#define setv_number(v, n)    set_value(TYPE_NUMBER,  as_number,  v, n)
#define setv_object(T, v, o) set_value(T,            as_object,  v, o)
#define setv_string(dst, src)   setv_object(TYPE_STRING, dst, src)
#define setv_table(dst, src)    setv_object(TYPE_TABLE,  dst, src)

#define is_falsy(v)         (is_nil(v) || (is_boolean(v) && !as_boolean(v)))

// Writes string representation of `self` to C `stdout`.
void print_value(const Value *self, bool quoted);

// Assumes buffer is a fixed-size array of length `MAX_TOSTRING`.
// If `out` is not `NULL`, it will be set to -1 if we do not own the result.
const char *to_cstring(const Value *self, char *buffer, int *out);

// We cannot use `memcmp` due to struct padding.
bool values_equal(const Value *lhs, const Value *rhs);

void init_varray(VArray *self);
void free_varray(VArray *self, Alloc *alloc);
void write_varray(VArray *self, const Value *value, Alloc *alloc);

// NOTE: For `concat_string` we do not know the correct hash yet.
// Analogous to `allocateString()` in the book.
String *new_string(int len, Alloc *alloc);
void free_string(String *self, Alloc *alloc);
// Global functions that deal with strings need the VM to check for interned.
String *copy_string(VM *vm, const StrView *view);
String *copy_lstring(VM *vm, const StrView *view);

// Assumes all arguments we already verified to be `String*`.
String *concat_strings(VM *vm, int argc, const Value argv[], int len);

// Used for user-created tables, not VM's globals/strings tables.
Table *new_table(Alloc *alloc);
void init_table(Table *self);
void free_table(Table *self, Alloc *alloc);
void dump_table(const Table *self, const char *name);
bool get_table(Table *self, const Value *key, Value *out);
bool set_table(Table *self, const Value *key, const Value *value, Alloc *alloc);

// Place a tombstone value. Analogous to `deleteTable()` in the book.
bool unset_table(Table *self, const Value *key);

// Analogous to `tableAddAll()` in the book.
void copy_table(Table *dst, const Table *src, Alloc *alloc);

// Mutates the `vm->strings` table. Maps strings to non-nil values.
void set_interned(VM *vm, const String *key);

// Searches for interned strings. Analogous to `tableFindString()` in the book.
String *find_interned(VM *vm, const StrView *view, uint32_t hash);

#endif /* LULU_OBJECT_H */
