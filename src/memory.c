#include "memory.h"
#include "object.h"
#include "string.h"
#include "table.h"
#include "vm.h"

void lulu_set_allocator(lulu_VM *vm, lulu_AllocFn fn, void *ctx)
{
    vm->allocator.allocate = fn;
    vm->allocator.context  = ctx;
}

void *lulu_new_pointer(lulu_VM *vm, size_t size)
{
    return vm->allocator.allocate(NULL, 0, size, vm->allocator.context);
}

void *lulu_resize_pointer(lulu_VM *vm, void *ptr, size_t oldsz, size_t newsz)
{

    return vm->allocator.allocate(ptr, oldsz, newsz, vm->allocator.context);
}

void lulu_free_pointer(lulu_VM *vm, void *ptr, size_t size)
{
    vm->allocator.allocate(ptr, size, 0, vm->allocator.context);
}

Object *lulu_new_object(lulu_VM *vm, size_t size, VType tag)
{
    Object *obj = lulu_new_pointer(vm, size);
    obj->tag    = tag;
    return lulu_prepend_object(vm, obj);
}

Object *lulu_prepend_object(lulu_VM *vm, Object *obj)
{
    obj->next   = vm->objects;
    vm->objects = obj;
    return obj;
}

Object *lulu_remove_object(lulu_VM *vm, Object *obj)
{
    vm->objects = obj->next;
    return obj;
}

static void free_object(lulu_VM *vm, Object *obj)
{
    switch (obj->tag) {
    case TYPE_STRING:
        free_string(vm, cast(String*, obj));
        break;
    case TYPE_TABLE:
        free_table(vm, cast(Table*, obj));
        lulu_free_pointer(vm, obj, sizeof(Table));
        break;
    default:
        eprintfln("[FATAL ERROR]:\nAttempt to free a %s", get_typename(obj));
        assert(false);
        break;
    }
}

void lulu_free_objects(lulu_VM *vm)
{
    Object *head = vm->objects;
    while (head != NULL) {
        Object *next = head->next;
        free_object(vm, head);
        head = next;
    }
}
