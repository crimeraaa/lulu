#ifndef LUA_OBJECT_H
#define LUA_OBJECT_H

#include "common.h"
#include "chunk.h"
#include "value.h"
#include "vm.h"

struct Object {
    VType type; // Unlike Lox, we use the same tag for objects.
    Object *next;   // Part of an instrusive linked list for GC.
};

struct LFunction {
    Object object;
    int arity;
    Chunk chunk;
    TString *name;
};

/**
 * III:24.7     Native Functions
 *
 * This is a function pointer type similar to the Lua C API `Proto`. In essence
 * all Lua-facing C functions MUST have an argument count and a pointer to the
 * first argument (i.e. pushed first) in the VM stack.
 *
 * NOTE:
 *
 * I've made it so that similar to the Lua C API, all C functions are able to
 * poke at their parent VM (the `lua_State*`). This is helpful to check if a
 * string has been interned, to intern new strings, etc.
 *
 * And instead of directly returning a value to `call_function()`, we push to
 * the VM's stack directly from within.
 */
typedef TValue (*NativeFn)(LVM *vm, int argc, TValue *argv);

/**
 * III:24.7     Native Functions
 *
 * Native or builtin functions are created directly within C itself, meaning
 * that the way we use them is a bit different. They don't create their own
 * CallFrame, we directly use C's.
 */
typedef struct {
    Object object;     // Metadata/GC info.
    NativeFn function; // C function pointer.
} CFunction;

struct TString {
    Object object; // Header for meta-information.
    DWord hash;    // Result of throwing `data` into a hash function.
    size_t len;    // Number of non-nul characters.
    char *data;    // Heap-allocated buffer.
};

/**
 * III:24.1     Function Objects
 *
 * Set up a blank function: 0 arity, no name and no code. All we do is allocate
 * memory for an object of type tag `LUA_TFUNCTION` and append it to the VM's
 * objects linked list.
 */
LFunction *new_function(LVM *vm);

CFunction *new_cfunction(LVM *vm, NativeFn function);

/**
 * III:19.4.1   Concatenation
 *
 * Given a heap-allocated pointer `buffer`, we "take ownership" by immediately
 * assigning it to a `TString*` instance instead of taking the time to allocate
 * a new pointer and copy contents.
 *
 * III:19.5     Freeing Objects
 *
 * We need to have a pointer to the VM in question so its objects linked list
 * can be updated accordingly.
 */
TString *take_string(LVM *vm, char *buffer, size_t len);

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
TString *copy_string(LVM *vm, const char *literal, size_t len);

/**
 * III:19.4     Operations on Strings
 *
 * We use a good 'ol `TValue` so that we can call the `Object*` header.
 */
void print_object(const TValue *value);

/* Given an `TValue*`, treat it as an `Object*` and get the type. */
#define objtype(v)          (asobject(v)->type)
#define isstring(v)         isobject(v, LUA_TSTRING)
#define asstring(v)         ((TString*)asobject(v))
#define ascstring(v)        (asstring(v)->data)
#define makestring(o)       makeobject(LUA_TSTRING, o)

#define isfunction(v)       isobject(v, LUA_TFUNCTION)
#define asfunction(v)       ((LFunction*)asobject(v))
#define makefunction(o)     makeobject(LUA_TFUNCTION, o)

#define iscfunction(v)      isobject(v, LUA_TNATIVE)
#define ascfunction(v)      ((CFunction*)asobject(v))
#define makecfunction(o)    makeobject(LUA_TNATIVE, o)

#endif /* LUA_OBJECT_H */
