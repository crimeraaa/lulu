#include "debug.h"
#include "table.h"
#include "vm.h"

Object *
object_new(lulu_VM *vm, Value_Type type, isize size)
{
    Object *object = cast(Object *)mem_alloc(vm, size);
    object->type = type;
    object->next = vm->objects;
    vm->objects  = object;
    return object;
}

void
object_free(lulu_VM *vm, Object *self)
{
    switch (self->type) {
    case LULU_TYPE_STRING:
        ostring_free(vm, cast(OString *)self);
        break;
    case LULU_TYPE_TABLE: {
        Table *table = cast(Table *)self;
        table_free(vm, table);
        ptr_free(Table, vm, table);
        break;
    }
    default:
        debug_fatalf("Attempt to free a %s value", LULU_TYPENAMES[self->type]);
        __builtin_unreachable();
    }
}
