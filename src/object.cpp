#include "object.hpp"
#include "vm.hpp"

void
object_free(lulu_VM *vm, Object *o)
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
        Chunk *p = &o->chunk;
        dynamic_delete(vm, &p->constants);
        dynamic_delete(vm, &p->code);
        dynamic_delete(vm, &p->lines);
        dynamic_delete(vm, &p->locals);
        mem_free(vm, p);
        break;
    }
    case VALUE_FUNCTION: {
        // We never allocate a `Function *` as-is!
        Closure *f = &o->function;
        if (f->is_c()) {
            mem_free(vm, &f->c);
        } else {
            mem_free(vm, &f->lua);
        }
        break;
    }
    default:
        lulu_unreachable();
        break;
    }
}
