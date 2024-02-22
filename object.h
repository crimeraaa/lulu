#ifndef LUA_OBJECT_H
#define LUA_OBJECT_H

#include "common.h"
#include "value.h"

/**
 * III:19.2     Struct Inheritance
 * 
 * For now, our only subtype for objects is strings. This is the specific object
 * tag for `TValue` with a primary type of `LUA_TOBJECT`.
 */
typedef enum {
    LUA_TSTRING,
} ObjType;

struct lua_Object {
    ObjType type;      // Tag type for all objects.
};

struct lua_String {
    lua_Object object; // Header for meta-information.
    int length;        // Number of non-nul characters.
    char *chars;       // Heap-allocated buffer.
};

/**
 * III:19.3     Strings
 * 
 * Allocate enough memory to copy the string `chars` byte for byte.
 * Currently, we don't have our hashtable implementation so we leak memory.
 */
lua_String *copy_string(const char *chars, int length);

/**
 * III:19.4     Operations on Strings
 * 
 * We use a generic `TValue` so that we can call the `lua_Object*` header.
 */
void print_object(TValue value);

/**
 * III:19.2     Struct Inheritance
 * 
 * We don't use a macro in case the argument to `value` has side effects.
 */
static inline bool isobjtype(TValue value, ObjType type) {
    return isobject(value) && asobject(value)->type == type;
}

/* Given an `TValue*`, treat it as an `lua_Object*` and get the type. */
#define objtype(value)      (asobject(value)->type)

#define isstring(value)     isobjtype(value, LUA_TSTRING)
#define asstring(value)     ((lua_String*)asobject(value))
#define ascstring(value)    (asstring(value)->chars)

#endif /* LUA_OBJECT_H */
