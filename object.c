#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

/* https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function#FNV_hash_parameters */
#define FNV32_OFFSET    0x811c9dc5
#define FNV32_PRIME     0x01000193

static lua_Object *allocate_object(lua_VM *lvm, Size size, ValueType type) {
    lua_Object *object = reallocate(NULL, 0, size);
    object->type = type;
    object->next = lvm->objects; // Update the VM's allocation linked list
    lvm->objects = object;
    return object;
}

#define allocate_object(vm, T, tag)     (T*)allocate_object(vm, sizeof(T), tag)

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
 */
static inline lua_String *allocate_string(lua_VM *lvm, char *data, Size len, DWord hash) {
    lua_String *result = allocate_object(lvm, lua_String, LUA_TSTRING);
    result->hash = hash;
    result->len  = len;
    result->data = data;
    // We don't care about the value for the interned strings, just the key.
    table_set(&lvm->strings, result, makenil);
    return result;
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
static DWord hash_string(const char *key, Size len) {
    DWord hash = FNV32_OFFSET;
    for (Size i = 0; i < len; i++) {
        hash ^= (Byte)key[i];
        hash *= FNV32_PRIME;
    }
    return hash;
}

lua_String *take_string(lua_VM *lvm, char *data, Size len) {
    DWord hash = hash_string(data, len);
    lua_String *interned = table_findstring(&lvm->strings, data, len, hash);
    if (interned != NULL) {
        free(data);
        return interned;
    }
    return allocate_string(lvm, data, len, hash);
}

lua_String *copy_string(lua_VM *lvm, const char *literal, Size len) {
    DWord hash = hash_string(literal, len);
    lua_String *interned = table_findstring(&lvm->strings, literal, len, hash);
    if (interned != NULL) {
        return interned;
    }
    char *data = allocate(char, len + 1);
    memcpy(data, literal, len);
    data[len] = '\0';
    return allocate_string(lvm, data, len, hash);
}

void print_object(TValue value) {
    switch (value.as.object->type) {
    case LUA_TSTRING: printf("%s", ascstring(value)); break;
    default: return;
    }
}

#undef allocate_famobject
#undef allocate_famstring
