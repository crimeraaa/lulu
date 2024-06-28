#include "memory.h"
#include "object.h"
#include "string.h"
#include "table.h"
#include "vm.h"
#include "api.h"

void *luluMem_call_allocator(lulu_VM *vm, void *ptr, size_t oldsz, size_t newsz)
{
    void *res = vm->allocator(ptr, oldsz, newsz, vm->context);
    // Non-zero allocation request failed? (Freeing uses `newsz` of 0).
    if (res == nullptr && newsz > 0)
        lulu_alloc_error(vm);
    return res;
}

Object *luluObj_new(lulu_VM *vm, size_t size, VType tag)
{
    Object *obj = luluMem_new_pointer(vm, size);
    obj->tag    = tag;
    return luluObj_link(vm, obj);
}

Object *luluObj_link(lulu_VM *vm, Object *obj)
{
    obj->next   = vm->objects;
    vm->objects = obj;
    return obj;
}

Object *luluObj_unlink(lulu_VM *vm, Object *obj)
{
    vm->objects = obj->next;
    return obj;
}

static void free_object(lulu_VM *vm, Object *obj)
{
    switch (obj->tag) {
    case TYPE_STRING:
        luluStr_free(vm, cast_string(obj));
        break;
    case TYPE_TABLE:
        luluTbl_free(vm, cast_table(obj));
        luluMem_free_pointer(vm, obj, sizeof(Table));
        break;
    default:
        eprintfln("[FATAL ERROR]:\nAttempt to free a %s", get_typename(obj));
        assert(false);
        break;
    }
}

void luluObj_free_all(lulu_VM *vm)
{
    Object *head = vm->objects;
    while (head != nullptr) {
        Object *next = head->next;
        free_object(vm, head);
        head = next;
    }
}
