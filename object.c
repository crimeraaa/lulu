#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

/* https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function#FNV_hash_parameters */
#define FNV32_OFFSET    0x811c9dc5
#define FNV32_PRIME     0x01000193

static lua_Object *allocate_object(LuaVM *lvm, size_t size, ObjType type) {
    lua_Object *object = reallocate(NULL, 0, size);
    object->type = type;
    object->next = lvm->objects; // Update the VM's allocation linked list
    lvm->objects = object;
    return object;
}

/**
 * III:19.2:    Strings
 * 
 * Wrapper around the above function as not all lua_Object instances are of the
 * same size. Sure they all have the same header, but that's about it.
 * 
 * III:19.5:    Freeing Objects
 * 
 * @param vm    A `LuaVM*`. We'll update its objects intrusive linked list.
 * @param stype Structure type.
 * @param dtype Flexible array member datatype.
 * @param len   Desired length of the array.
 * @param tag   Specific object tag, e.g. `LUA_TSTRING`.
 */
#define allocate_famobject(vm, stype, dtype, len, tag) \
    (stype*)allocate_object(vm, sizeof(stype) + sizeof(dtype[len]), tag)

/**
 * Helper macro because the above is nigh unreadable...
 */
#define allocate_famstring(vm, len) \
    allocate_famobject(vm, lua_String, char, len, LUA_TSTRING)

/**
 * III:19.2     Strings
 * 
 * Create a new lua_String on the heap and "take ownership" of the given buffer.
 * 
 * III:19.5     Freeing Objects
 * 
 * Because I *don't* want to use global state, we have to pass in a VM pointer!
 * But because this function can be called during the compile phase, the compiler
 * structure must also have a pointer to a VM...
 * 
 * III:19.1:   Flexible Array Members (CHALLENGE)
 * 
 * To reduce 2 heap allocations to 1 for each `lua_String*`, we can use a C99
 * concept known as "flexible array members". It's a bit tricky to work with,
 * and we can't make arrays of the structures themselves (but we can for pointers).
 * 
 * So instead of using a function like `take_string()`, you'll need to explicitly
 * allocate this the moment you see a char buffer you want to take ownership of.
 */
static inline lua_String *allocate_string(LuaVM *lvm, int length) {
    lua_String *res = allocate_famstring(lvm, length + 1);
    res->length = length;
    // We don't care about the value for the interned strings, just the key.
    table_set(&lvm->strings, res, makenil);
    return res;
}

/**
 * III:20.4.1   Hashing strings
 * 
 * This is the FNV-1A hash function which is pretty neat. It's not the most
 * cryptographically secure or whatnot, but it's something we can work with.
 * 
 * Unfortunately due to how we allocate the lua_String pointers, we have to
 * determine the hash AFTER writing the data pointer as we don't have access to
 * the full string in functions like `concat_strings()`.
 */
static uint32_t hash_string(const char *key, int length) {
    uint32_t hash = FNV32_OFFSET;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= FNV32_PRIME;
    }
    return hash;
}

// lua_String *take_string(LuaVM *lvm, char *buffer, int length) {
//     return allocate_string(lvm, buffer, length);
// }

lua_String *copy_string(LuaVM *lvm, const char *literal, int length) {
    // TODO: III:20.5 String Interning
    lua_String *result = allocate_string(lvm, length);
    memcpy(result->data, literal, result->length);
    result->data[result->length] = '\0';
    result->hash = hash_string(result->data, result->length);
    return result;
}

lua_String *concat_strings(LuaVM *lvm, const lua_String *lhs, const lua_String *rhs) {
    lua_String *result = allocate_string(lvm, lhs->length + rhs->length);
    memcpy(result->data, lhs->data, lhs->length);
    // Concatenation starts at 1 past the position of lhs in res
    memcpy(result->data + lhs->length, rhs->data, rhs->length);
    result->data[result->length] = '\0';
    result->hash = hash_string(result->data, result->length);
    return result;
}

void print_object(TValue value) {
    switch (value.as.object->type) {
    case LUA_TSTRING: printf("%s", ascstring(value)); break;
    }
}

#undef allocate_famobject
#undef allocate_famstring
