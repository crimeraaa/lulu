#include "object.hpp"
#include "vm.hpp"

void
object_free(lulu_VM &vm, Object *o)
{
    switch (o->base.type) {
    case VALUE_STRING: {
        OString *s = &o->ostring;
        mem_free(vm, s, s->len);
        break;
    }
    default:
        lulu_unreachable();
        break;
    }
}
