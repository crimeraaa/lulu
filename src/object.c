#include "debug.h"
#include "table.h"
#include "vm.h"

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
    case LULU_TYPE_TABLE: {
        lulu_Table *table = cast(lulu_Table *)self;
        lulu_Table_free(vm, table);
        rawptr_free(lulu_Table, vm, table);
        break;
    }
    default:
        lulu_Debug_fatalf("Attempt to free a %s value", LULU_TYPENAMES[self->type]);
        __builtin_unreachable();
    }
}
