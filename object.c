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
    // Update the VM's allocation linked list
    object->next = lvm->objects;
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
 * @param type  Structure type.
 * @param tag   Specific object tag, e.g. `LUA_TSTRING`.
 */
#define allocate_object(vm, type, tag)  (type*)allocate_object(vm, sizeof(type), tag)

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
static lua_String *allocate_string(LuaVM *lvm, char *buffer, int length) {
    lua_String *string = allocate_object(lvm, lua_String, LUA_TSTRING);
    string->length = length;
    string->data = buffer;
    return string;
}

lua_String *take_string(LuaVM *lvm, char *buffer, int length) {
    return allocate_string(lvm, buffer, length);
}

lua_String *copy_string(LuaVM *lvm, const char *literal, int length) {
    // nul terminate for good measure.
    char *buffer = allocate(char, length + 1);
    memcpy(buffer, literal, length);
    buffer[length] = '\0';
    return allocate_string(lvm, buffer, length);
}

void print_object(TValue value) {
    switch (value.as.object->type) {
    case LUA_TSTRING: printf("%s", ascstring(value)); break;
    }
}
