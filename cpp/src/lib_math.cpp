#include <math.h>

#include "lulu_auxlib.h"

template<lulu_Number (*Func)(lulu_Number)>
static int
math_fn(lulu_VM *L)
{
    lulu_Number x = lulu_check_number(L, 1);
    lulu_push_number(L, Func(x));
    return 1;
}

template<lulu_Number (*Func)(lulu_Number, lulu_Number)>
static int
math_fn2(lulu_VM *L)
{
    lulu_Number x = lulu_check_number(L, 1);
    lulu_Number y = lulu_check_number(L, 2);
    lulu_push_number(L, Func(x, y));
    return 1;
}

static int
math_log(lulu_VM *L)
{
    lulu_Number n = lulu_check_number(L, 1);
    if (lulu_is_none_or_nil(L, 2)) {
        // math.h:log() uses base e.
        lulu_push_number(L, log(n));
    } else {
        lulu_Number b = lulu_check_number(L, 2);
        // log_b(x) == log_e(x) / log_e(b) == log_10(x) / log_10(b)
        lulu_push_number(L, log(n) / log(b));
    }
    return 1;
}

static int
math_max(lulu_VM *L)
{
    int         n_args = lulu_get_top(L);
    lulu_Number m      = lulu_check_number(L, 1);

    // Check all succeeding arguments against the first one.
    for (int i = 2; i < n_args; i++) {
        lulu_Number n = lulu_check_number(L, i);
        if (n > m) {
            m = n;
        }
    }

    lulu_push_number(L, m);
    return 1;
}

static int
math_min(lulu_VM *L)
{
    int         n_args = lulu_get_top(L);
    lulu_Number m      = lulu_check_number(L, 1);

    for (int i = 2; i < n_args; i++) {
        lulu_Number n = lulu_check_number(L, i);
        if (n < m) {
            m = n;
        }
    }
    lulu_push_number(L, m);
    return 1;
}

static int
math_modf(lulu_VM *L)
{
    lulu_Number x = lulu_check_number(L, 1);
    lulu_Number integer;
    lulu_Number fraction = modf(x, &integer);
    lulu_push_number(L, integer);
    lulu_push_number(L, fraction);
    return 2;
}

static int
math_frexp(lulu_VM *L)
{
    // Default value when `frac == NaN` or `frac == inf`.
    int         exponent = 0;
    lulu_Number x        = lulu_check_number(L, 1);
    lulu_Number fraction = frexp(x, &exponent);
    lulu_push_number(L, fraction);
    lulu_push_integer(L, exponent);
    return 2;
}


static int
math_ldexp(lulu_VM *L)
{
    lulu_Number  mantissa = lulu_check_number(L, 1);
    lulu_Integer exponent = lulu_check_integer(L, 2);

    // Equivalent to mantissa * 2^exponent.
    lulu_Number x = ldexp(mantissa, exponent);
    lulu_push_number(L, x);
    return 1;
}

static const lulu_Register math_lib[] = {
    {"abs", &math_fn<fabs>},
    {"acos", &math_fn<acos>},
    {"asin", &math_fn<asin>},
    {"atan", &math_fn<atan>},
    {"atan2", &math_fn2<atan2>},
    {"cbrt", &math_fn<cbrt>},
    {"ceil", &math_fn<ceil>},
    {"cos", &math_fn<cos>},
    {"cosh", &math_fn<cosh>},
    {"exp", &math_fn<exp>},
    {"exp2", &math_fn<exp2>},
    {"floor", &math_fn<floor>},
    {"fmod", &math_fn2<fmod>},
    {"frexp", &math_frexp},
    {"ldexp", &math_ldexp},
    {"log", &math_log},
    {"log2", &math_fn<log2>},
    {"log10", &math_fn<log10>},
    {"max", &math_max},
    {"min", &math_min},
    {"modf", &math_modf},
    {"pow", &math_fn2<powf64>},
    {"remainder", &math_fn2<remainder>},
    {"sin", &math_fn<sin>},
    {"sinh", &math_fn<sinh>},
    {"sqrt", &math_fn<sqrt>},
    {"tan", &math_fn<tan>},
    {"tanh", &math_fn<tanh>},
};

LULU_LIB_API int
lulu_open_math(lulu_VM *L)
{
    lulu_set_library(L, LULU_MATH_LIB_NAME, math_lib);

    /* constants */
    lulu_push_number(L, M_E);
    lulu_set_field(L, -2, "e");

    lulu_push_number(L, M_PI);
    lulu_set_field(L, -2, "pi");

    lulu_push_number(L, 2 * M_PI);
    lulu_set_field(L, -2, "tau");

    lulu_push_number(L, INFINITY);
    lulu_set_field(L, -2, "inf");

    lulu_push_number(L, NAN);
    lulu_set_field(L, -2, "nan");

    lulu_push_number(L, HUGE_VAL);
    lulu_set_field(L, -2, "huge");
    return 1;
}
