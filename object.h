#ifndef LUA_OBJECT_H
#define LUA_OBJECT_H

#include "common.h"
#include "value.h"
#include "vm.h"

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
    struct lua_Object *next; // Part of an instrusive linked list for GC.
};

struct lua_String {
    lua_Object object; // Header for meta-information.
    int length;        // Number of non-nul characters.
    char data[];        // Heap-allocated buffer.
};

/**
 * III:19.4.1   Concatenation
 * 
 * Given a heap-allocated pointer `buffer`, we "take ownership" by immediately
 * assigning it to a `lua_String*` instance instead of taking the time to allocate
 * a new pointer and copy contents.
 * 
 * III:19.5     Freeing Objects
 * 
 * We need to have a pointer to the VM in question so its objects linked list
 * can be updated accordingly.
 */
// lua_String *take_string(LuaVM *lvm, char *buffer, int length);

/**
 * III:19.3     Strings
 * 
 * Allocate enough memory to copy the string `literal` byte for byte.
 * Currently, we don't have our hashtable implementation so we leak memory.
 * 
 * III:19.5     Freeing Objects
 * 
 * We take a pointer to the VM so we can update the objects linked list right.
 * It's a bit silly that we also needed to add a VM pointer member to the
 * Compiler struct, but it'll do since the VM is always initialized before the
 * compiler ever is.
 */
lua_String *copy_string(LuaVM *lvm, const char *literal, int length);


/**
 * III:19.1     Flexible Array Members (CHALLENGE)
 * 
 * To make things easier, I've created a dedicated function to concatenate
 * 2 Lua strings. We allocate memory for a new string and copy the left into the
 * new string, then the right (starting from an offset into the new string).
 */
lua_String *concat_strings(LuaVM *lvm, const lua_String *lhs, const lua_String *rhs);

/**
 * III:19.4     Operations on Strings
 * 
 * We use a good 'ol `TValue` so that we can call the `lua_Object*` header.
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
#define ascstring(value)    (asstring(value)->data)

#endif /* LUA_OBJECT_H */
