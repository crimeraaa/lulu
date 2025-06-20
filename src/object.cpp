#include "object.hpp"
#include "vm.hpp"

void
object_link(Object **list, Object *o)
{
    o->prev = *list;
    *list   = o;
}


void
object_free(lulu_VM &vm, Object *o)
{
    unused(vm);
    switch (o->type) {
    // strings are only managed by `Intern`.
    default:
        lulu_unreachable();
        break;
    }
}
