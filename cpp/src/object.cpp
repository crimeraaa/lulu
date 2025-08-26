#include "object.hpp"
#include "vm.hpp"

void
object_free(lulu_VM *vm, Object *o)
{
    Value_Type t = o->base.type;
    switch (t) {
    case VALUE_STRING: {
        OString *s = &o->ostring;
        mem_free(vm, s, s->len);
        break;
    }
    case VALUE_TABLE:
        table_delete(vm, &o->table);
        break;
    case VALUE_CHUNK:
        chunk_delete(vm, &o->chunk);
        break;
    case VALUE_FUNCTION:
        closure_delete(vm, &o->function);
        break;
    case VALUE_UPVALUE:
        mem_free(vm, &o->upvalue);
        break;
    default:
        lulu_panicf("Invalid object (Value_Type(%i))", t);
        lulu_unreachable();
        break;
    }
}
