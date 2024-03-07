#ifndef LUA_OBJECT_H
#define LUA_OBJECT_H

#include "common.h"
#include "chunk.h"
#include "value.h"
#include "vm.h"

struct Object {
    ValueType type; // Unlike Lox, we use the same tag for objects.
    Object *next;   // Part of an instrusive linked list for GC.
};

struct LFunction {
    Object object;
    int arity;
    Chunk chunk;
    TString *name;
};

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
void print_object(TValue value);

/* Given an `TValue*`, treat it as an `Object*` and get the type. */
#define objtype(value)      (asobject(value)->type)
#define isstring(value)     isobject(value, LUA_TSTRING)
#define asstring(value)     ((TString*)asobject(value))
#define ascstring(value)    (asstring(value)->data)

#define isfunction(value)   isobject(value, LUA_TFUNCTION)
#define asfunction(value)   ((LFunction*)asobject(value))

#endif /* LUA_OBJECT_H */
