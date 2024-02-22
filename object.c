#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

static lua_Object *allocate_object(size_t size, ObjType type) {
    lua_Object *object = reallocate(NULL, 0, size);
    object->type = type;
    return object;
}

/**
 * III:19.2     Strings
 * 
 * Wrapper around the above function as not all lua_Object instances are of the
 * same size. Sure they all have the same header, but that's about it.
 */
#define allocate_object(type, tag)  (type*)allocate_object(sizeof(type), tag)

/**
 * III:19.2     Strings
 * 
 * Create a new lua_String on the heap and "take ownership" of the given buffer.
 */
static lua_String *allocate_string(char *buffer, int length) {
    lua_String *string = allocate_object(lua_String, LUA_TSTRING);
    string->length = length;
    string->data = buffer;
    return string;
}

lua_String *take_string(char *buffer, int length) {
    return allocate_string(buffer, length);
}

lua_String *copy_string(const char *literal, int length) {
    // nul terminate for good measure.
    char *buffer = allocate(char, length + 1);
    memcpy(buffer, literal, length);
    buffer[length] = '\0';
    return allocate_string(buffer, length);
}

void print_object(TValue value) {
    switch (value.as.object->type) {
    case LUA_TSTRING: printf("%s", ascstring(value)); break;
    }
}
