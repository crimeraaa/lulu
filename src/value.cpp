#include <stdio.h>

#include "value.hpp"
#include "string.hpp"

bool
value_eq(Value a, Value b)
{
    if (a.type != b.type) {
        return false;
    }

    switch (a.type) {
    case LULU_TYPE_NIL:     return true;
    case LULU_TYPE_BOOLEAN: return a.boolean == b.boolean;
    case LULU_TYPE_NUMBER:  return lulu_Number_eq(a.number, b.number);
    case LULU_TYPE_STRING:  return a.ostring == b.ostring;
    }
    lulu_unreachable();
}

void
value_print(Value v)
{
    switch (v.type) {
    case LULU_TYPE_NIL:
        fputs("nil", stdout);
        break;
    case LULU_TYPE_BOOLEAN:
        fputs((v.boolean) ? "true" : "false", stdout);
        break;
    case LULU_TYPE_NUMBER:
        fprintf(stdout, LULU_NUMBER_FMT, v.number);
        break;
    case LULU_TYPE_STRING:
        fputs(v.ostring->data, stdout);
        break;
    }
}
