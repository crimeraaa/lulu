#include <stdio.h>

#include "value.hpp"

bool
value_eq(Value a, Value b)
{
    if (a.type != b.type) {
        return false;
    }

    switch (a.type) {
    case LULU_TYPE_NIL:     return true;
    case LULU_TYPE_BOOLEAN: return a.boolean == b.boolean;
    case LULU_TYPE_NUMBER:  return a.number == b.number;
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
    }
}
