#include <stdio.h>

#include "value.hpp"
#include "object.hpp"

bool
operator==(Value a, Value b)
{
    if (a.type != b.type) {
        return false;
    }

    switch (a.type) {
    case VALUE_NONE:        break;
    case VALUE_NIL:         return true;
    case VALUE_BOOLEAN:     return value_to_boolean(a) == value_to_boolean(b);
    case VALUE_NUMBER:      return lulu_Number_eq(value_to_number(a), value_to_number(b));
    case VALUE_STRING:      // fall-through
    case VALUE_TABLE:
    case VALUE_FUNCTION:    return value_to_object(a) == value_to_object(b);
    case VALUE_CHUNK:       break;
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
        fputs(value_to_boolean(v) ? "true" : "false", stdout);
        break;
    case VALUE_NUMBER:
        fprintf(stdout, LULU_NUMBER_FMT, value_to_number(v));
        break;
    case VALUE_STRING: {
        OString *s = value_to_ostring(v);
        char     q = (s->len == 1) ? '\'' : '\"';
        fprintf(stdout, "%c%s%c", q, s->data, q);
        break;
    }
    case VALUE_TABLE:
    case VALUE_FUNCTION:
        fprintf(stdout, "%s: %p", value_type_name(t), cast(void *)value_to_object(v));
        break;
    case VALUE_NONE:
    case VALUE_CHUNK:
        lulu_unreachable();
        break;
    }
}
