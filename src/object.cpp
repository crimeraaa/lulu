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
        slice_delete(vm, t->entries);
        mem_free(vm, t);
        break;
    }
    case VALUE_CHUNK: {
        Chunk *c = &o->chunk;
        dynamic_delete(vm, c->constants);
        dynamic_delete(vm, c->code);
        dynamic_delete(vm, c->line_info);
        mem_free(vm, c);
        break;
    }
    default:
        lulu_unreachable();
        break;
    }
}
