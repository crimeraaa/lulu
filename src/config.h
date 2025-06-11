#pragma once

#define LULU_NUMBER_TYPE double
#define LULU_NUMBER_FMT  "%.14g"

#ifdef LULU_BUILD

#include <math.h>

#define lulu_Number_add(x, y)   ((x) + (y))
#define lulu_Number_sub(x, y)   ((x) - (y))
#define lulu_Number_mul(x, y)   ((x) * (y))
#define lulu_Number_div(x, y)   ((x) / (y))
#define lulu_Number_mod(x, y)   fmod(x, y)
#define lulu_Number_pow(x, y)   fmod(x, y)
#define lulu_Number_unm(x)      (-(x))

#endif /* LULU_BUILD */
