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
    case VALUE_TABLE: {
        Table *t = &o->table;
        mem_delete(vm, raw_data(t->entries), len(t->entries));
        break;
    }
    default:
        lulu_unreachable();
        break;
    }
}
