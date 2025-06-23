#include <stdio.h>

#include "value.hpp"
#include "string.hpp"

bool
operator==(Value a, Value b)
{
    if (a.type != b.type) {
        return false;
    }

    switch (a.type) {
    case VALUE_NIL:     return true;
    case VALUE_BOOLEAN: return a.boolean == b.boolean;
    case VALUE_NUMBER:  return lulu_Number_eq(a.number, b.number);
    case VALUE_STRING:  // fall-through
    case VALUE_TABLE:   return a.object == b.object;
    case VALUE_CHUNK:
        break;
    }
    lulu_unreachable();
}

void
value_print(Value v)
{
    Value_Type t = v.type;
    switch (t) {
    case VALUE_NIL:
        fputs("nil", stdout);
        break;
    case VALUE_BOOLEAN:
        fputs((v.boolean) ? "true" : "false", stdout);
        break;
    case VALUE_NUMBER:
        fprintf(stdout, LULU_NUMBER_FMT, v.number);
        break;
    case VALUE_STRING: {
        OString *s = &v.object->ostring;
        char     q = (s->len == 1) ? '\'' : '\"';
        fprintf(stdout, "%c%s%c", q, s->data, q);
        break;
    }
    case VALUE_TABLE:
        fprintf(stdout, "%s: %p", value_type_name(t), cast(void *)v.object);
        break;
    case VALUE_CHUNK:
        lulu_unreachable();
        break;
    }
}
