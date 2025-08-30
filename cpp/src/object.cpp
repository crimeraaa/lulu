#include "object.hpp"
#include "vm.hpp"


void
object_free(lulu_VM *L, Object *o)
{
    Value_Type t = o->type();
#ifdef LULU_DEBUG_LOG_GC
    object_gc_print(o, ANSI_TEXT_RED("[FREE]"));
#endif
    switch (t) {
    case VALUE_STRING: {
        OString *s = &o->ostring;
        mem_free(L, s, s->len);
        break;
    }
    case VALUE_TABLE: table_delete(L, &o->table); break;
    case VALUE_CHUNK: chunk_delete(L, &o->chunk); break;
    case VALUE_FUNCTION: closure_delete(L, &o->function); break;
    case VALUE_UPVALUE: mem_free(L, &o->upvalue); break;
    default:
        lulu_panicf("Invalid object (Value_Type(%i))", t);
        break;
    }
}

#ifdef LULU_DEBUG_LOG_GC

void
object_gc_print(Object *o, const char *fmt, ...)
{
    char buf[128];
    const char *s;
    if (o->type() == VALUE_STRING) {
        s = o->ostring.to_cstring();
    } else {
        void *p = static_cast<void *>(o);
        const char *t = o->type_name();
        sprintf(buf, "%s: %p", t, p);
        s = buf;
    }

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    if (o->type() == VALUE_STRING) {
        printf(" \"%s\"\n", s);
    } else {
        printf(" %s\n", s);
    }
}

#endif // LULU_DEBUG_LOG_GC
