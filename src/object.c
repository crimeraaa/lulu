#include "object.h"
#include "debug.h"
#include "vm.h"

#include <string.h>

lulu_Object *
lulu_Object_new(lulu_VM *vm, lulu_Value_Type type, isize size)
{
    lulu_Object *object = cast(lulu_Object *)lulu_Memory_alloc(vm, size);
    object->type = type;
    object->next = vm->objects;
    vm->objects  = object;
    return object;
}

void
lulu_Object_free(lulu_VM *vm, lulu_Object *self)
{
    switch (self->type) {
    case LULU_TYPE_STRING:
        lulu_String_free(vm, cast(lulu_String *)self);
        break;
    default:
        lulu_Debug_fatalf("Attempt to free a %s value", LULU_TYPENAMES[self->type]);
        __builtin_unreachable();
    }
}

lulu_String *
lulu_String_new(lulu_VM *vm, lulu_String_View src)
{
    isize        vla_size = size_of(src.data[0]) * (src.len + 1);
    lulu_String *string   = cast(lulu_String *)lulu_Object_new(vm, LULU_TYPE_STRING,
        size_of(lulu_String) + vla_size);

    string->len = src.len;
    string->data[src.len] = '\0';
    memcpy(string->data, src.data, src.len);
    return string;

}

void
lulu_String_free(lulu_VM *vm, lulu_String *self)
{
    isize vla_size = size_of(self->data[0]) * (self->len + 1);
    lulu_Memory_free(vm, self, size_of(*self) + vla_size);
}
