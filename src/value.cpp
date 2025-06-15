#include <stdio.h>

#include "value.hpp"

bool
value_eq(Value a, Value b)
{
    return a == b;
}

void
value_print(Value v)
{
    printf(LULU_NUMBER_FMT, v);
}
