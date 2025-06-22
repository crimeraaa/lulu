#include "object.hpp"
#include "vm.hpp"

void
object_free(lulu_VM &vm, Object *o)
{
    switch (o->type) {
    case LULU_TYPE_STRING: {
        OString *s = cast(OString *)o;
        mem_free(vm, s, s->len);
        break;
    }
    default:
        lulu_unreachable();
        break;
    }
}
