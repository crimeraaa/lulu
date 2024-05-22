#include "memory.h"
#include "object.h"
#include "string.h"
#include "table.h"
#include "vm.h"

void init_alloc(Alloc *al, ReallocFn fn, void *ctx)
{
    al->reallocfn = fn;
    al->context   = ctx;
}

struct lulu_Object *new_object(size_t size, VType tag, Alloc *al)
{
    VM     *vm = al->context;
    Object *o  = new_pointer(size, al);
    o->tag     = tag;
    return prepend_object(&vm->objects, o);
}

struct lulu_Object *prepend_object(struct lulu_Object **head, struct lulu_Object *o)
{
    o->next = *head;
    *head   = o;
    return o;
}

struct lulu_Object *remove_object(struct lulu_Object **head, struct lulu_Object *o)
{
    *head = o->next;
    return o;
}

static void free_object(Object *object, Alloc *al)
{
    switch (object->tag) {
    case TYPE_STRING:
        free_string(cast(String*, object), al);
        break;
    case TYPE_TABLE:
        free_table(cast(Table*, object), al);
        free_pointer(object, sizeof(Table), al);
        break;
    default:
        eprintfln("[FATAL ERROR]:\nAttempt to free a %s", get_typename(object));
        assert(false);
        break;
    }
}

void free_objects(struct lulu_VM *vm)
{
    Alloc  *al   = &vm->allocator;
    Object *head = vm->objects;
    while (head != NULL) {
        Object *next = head->next;
        free_object(head, al);
        head = next;
    }
}
