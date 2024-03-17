#ifndef LUA_VALUE_H
#define LUA_VALUE_H

#include "common.h"
#include "conf.h"

/** 
 * The tag part of the tagged union `TValue`. 
 *
 * See: https://www.lua.org/source/5.1/ltm.c.html#luaT_typenames
 */
typedef enum {
    LUA_TBOOLEAN,
    LUA_TFUNCTION,
    LUA_TNIL,
    LUA_TNUMBER,
    LUA_TSTRING,
    LUA_TTABLE,
    LUA_TNONE,      // Dummy type, distinct from nil, used for no args errors.
    LUA_TCOUNT,     // Not an actual type, used for the typenames array only.
    LUA_TUSIZE,     // Put after count so it's never a valid user-facing type.
} VType;

/**
 * Despite starting with 'T', it stands for 'Type' and not 'Tag'. This is just
 * a string literal and its nul-terminated length for each possible Lua type.
 */
typedef struct {
    const char *what;
    size_t len;
} TNameInfo;

/**
 * Given a `tag`, retrieve a pointer into the `typenames` array.
 * You can use this to extract information about typenames, which is really just
 * their literal representation and their string length.
 */
const TNameInfo *get_tnameinfo(VType tag);

typedef LUA_NUMBER lua_Number;

struct Object {
    VType type;     // Unlike Lox, we use the same tag for objects.
    Object *next;   // Part of an instrusive linked list for GC.
};

struct TValue {
    VType type; // Tag for the union to ensure some type safety.
    union {
        bool boolean;      // `true` and `false,` nothing more, nothing less.
        lua_Number number; // Our one and only numerical datatype.
        size_t usize;      // Internal use only, specifically in chunk.c.
        Object *object;    // Heap-allocated and garbage-collectible.
    } as; // Actual value contained within this struct. Be very careful!
};

typedef struct {
    TValue *array; // 1D array of Lua values.
    size_t count;
    size_t cap;
} TArray;

void init_tarray(TArray *self);
void write_tarray(TArray *self, const TValue *value);
void free_tarray(TArray *self);
void print_value(const TValue *value);

/**
 * III:18.4.2   Equality and comparison operators
 * 
 * Given 2 dynamically typed values, how do we compare them? Well, if they're of
 * different types, you can automatically assume they're not the same.
 * 
 * Otherwise, we'll need to do a comparison on a type-by-type basis.
 * 
 * NOTE:
 * 
 * We CANNOT use memcmp as it's likely the compiler added padding, which goes
 * unused. If we do raw memory comparisons we'll also compare these garbage bits
 * which will not give us the results we want.
 */
bool values_equal(const TValue *lhs, const TValue *rhs);

#define makeboolean(b)      ((TValue){LUA_TBOOLEAN, {.boolean = (b)}})
#define makenil             ((TValue){LUA_TNIL,     {.number = 0.0}})
#define makenone            ((TValue){LUA_TNONE,    {.number = 0.0}})
#define makenumber(n)       ((TValue){LUA_TNUMBER,  {.number = (n)}})

/** 
 * Wrap a bare object pointer or a specific object type pointer into a somewhat
 * generalized struct, but we DO need the specific tag.
 * 
 * NOTE:
 * 
 * This one of my derivations from Lox as Lua doesn't have dedicated object type
 * in their C API. Everything is packaged into the same enum.
 */
#define makeobject(T, o)    ((TValue){T, {.object = (Object*)(o)}})

#define asboolean(v)        ((v)->as.boolean)
#define asnumber(v)         ((v)->as.number)
#define asobject(v)         ((v)->as.object)

#define isboolean(v)        ((v)->type == LUA_TBOOLEAN)
#define isexactlytrue(v)    (isboolean(v) && asboolean(v))
#define isexactlyfalse(v)   !isexactlytrue(v)

#define isfalsy(v)          ((isnil(v)) || (isboolean(v) && !asboolean(v)))
#define isnil(v)            ((v)->type == LUA_TNIL)
#define isnone(v)           ((v)->type == LUA_TNONE)
#define isnilornone(v)      (isnil(v) || isnone(v))
#define isnumber(v)         ((v)->type == LUA_TNUMBER)
#define isobject(T, v)      ((v)->type == T)

#endif /* LUA_VALUE_H */
