#include "memory.h"
#include "object.h"
#include "string.h"
#include "table.h"
#include "vm.h"

void init_alloc(Alloc *al, AllocFn fn, void *ctx)
{
    al->allocfn = fn;
    al->context = ctx;
}

Object *new_object(size_t size, VType tag, Alloc *al)
{
    VM     *vm  = al->context;
    Object *obj = new_pointer(size, al);
    obj->tag    = tag;
    return prepend_object(&vm->objects, obj);
}

Object *prepend_object(Object **head, Object *obj)
{
    obj->next = *head;
    *head     = obj;
    return obj;
}

Object *remove_object(Object **head, Object *obj)
{
    *head = obj->next;
    return obj;
}

static void free_object(Object *obj, Alloc *al)
{
    switch (obj->tag) {
    case TYPE_STRING:
        free_string(cast(String*, obj), al);
        break;
    case TYPE_TABLE:
        free_table(cast(Table*, obj), al);
        free_pointer(obj, sizeof(Table), al);
        break;
    default:
        eprintfln("[FATAL ERROR]:\nAttempt to free a %s", get_typename(obj));
        assert(false);
        break;
    }
}

void free_objects(lulu_VM *vm)
{
    Alloc  *al   = &vm->allocator;
    Object *head = vm->objects;
    while (head != NULL) {
        Object *next = head->next;
        free_object(head, al);
        head = next;
    }
}
