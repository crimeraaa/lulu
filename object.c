#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

static lua_Object *allocate_object(LuaVM *lvm, size_t size, ObjType type) {
    lua_Object *object = reallocate(NULL, 0, size);
    object->type = type;
    object->next = lvm->objects; // Update the VM's allocation linked list
    lvm->objects = object;
    return object;
}

/**
 * III:19.2     Strings
 * 
 * Wrapper around the above function as not all lua_Object instances are of the
 * same size. Sure they all have the same header, but that's about it.
 * 
 * III:19.5     Freeing Objects
 * 
 * @param vm    A `LuaVM*`. We'll update its objects intrusive linked list.
 * @param stype Structure type.
 * @param dtype Flexible array member datatype.
 * @param len   Desired length of the array.
 * @param tag   Specific object tag, e.g. `LUA_TSTRING`.
 */
#define allocate_famobject(vm, stype, dtype, len, tag) \
    (stype*)allocate_object(vm, sizeof(stype) + sizeof(dtype[len]), tag)

#define allocate_famstring(vm, len) \
    allocate_famobject(vm, lua_String, char, len, LUA_TSTRING)

lua_String *allocate_string(LuaVM *lvm, int length) {
    lua_String *res = allocate_famstring(lvm, length + 1);
    res->length = length;
    return res;
}

// lua_String *take_string(LuaVM *lvm, char *buffer, int length) {
//     return allocate_string(lvm, buffer, length);
// }

lua_String *copy_string(LuaVM *lvm, const char *literal, int length) {
    lua_String *result = allocate_string(lvm, length);
    memcpy(result->data, literal, result->length);
    result->data[result->length] = '\0';
    return result;
}

lua_String *concat_strings(LuaVM *lvm, const lua_String *lhs, const lua_String *rhs) {
    lua_String *result = allocate_string(lvm, lhs->length + rhs->length);
    memcpy(result->data, lhs->data, lhs->length);
    // Concatenation starts at 1 past the position of lhs in res
    memcpy(result->data + lhs->length, rhs->data, rhs->length);
    result->data[result->length] = '\0';
    return result;
}

void print_object(TValue value) {
    switch (value.as.object->type) {
    case LUA_TSTRING: printf("%s", ascstring(value)); break;
    }
}

#undef allocate_famobject
#undef allocate_famstring
