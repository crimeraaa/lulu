#include <math.h>

#include "lulu_auxlib.h"

template<lulu_Number (*Func)(lulu_Number)>
static int
math_fn(lulu_VM *vm)
{
    lulu_Number x = lulu_check_number(vm, 1);
    lulu_push_number(vm, Func(x));
    return 1;
}

template<lulu_Number (*Func)(lulu_Number, lulu_Number)>
static int
math_fn2(lulu_VM *vm)
{
    lulu_Number x = lulu_check_number(vm, 1);
    lulu_Number y = lulu_check_number(vm, 2);
    lulu_push_number(vm, Func(x, y));
    return 1;
}

static int
math_log(lulu_VM *vm)
{
    lulu_Number n = lulu_check_number(vm, 1);
    if (lulu_is_none_or_nil(vm, 2)) {
        // math.h:log() uses base e.
        lulu_push_number(vm, log(n));
    } else {
        lulu_Number b = lulu_check_number(vm, 2);
        // log_b(x) == log_e(x) / log_e(b) == log_10(x) / log_10(b)
        lulu_push_number(vm, log(n) / log(b));
    }
    return 1;
}

static int
math_max(lulu_VM *vm)
{
    int n_args = lulu_get_top(vm);
    lulu_Number m = lulu_check_number(vm, 1);

    // Check all succeeding arguments against the first one.
    for (int i = 2; i < n_args; i++) {
        lulu_Number n = lulu_check_number(vm, i);
        if (n > m) {
            m = n;
        }
    }

    lulu_push_number(vm, m);
    return 1;
}

static int
math_min(lulu_VM *vm)
{
    int n_args = lulu_get_top(vm);
    lulu_Number m = lulu_check_number(vm, 1);

    for (int i = 2; i < n_args; i++) {
        lulu_Number n = lulu_check_number(vm, i);
        if (n < m) {
            m = n;
        }
    }
    lulu_push_number(vm, m);
    return 1;
}

static int
math_modf(lulu_VM *vm)
{
    lulu_Number x = lulu_check_number(vm, 1);
    lulu_Number whole;
    lulu_Number frac = modf(x, &whole);
    lulu_push_number(vm, whole);
    lulu_push_number(vm, frac);
    return 2;
}

static int
math_frexp(lulu_VM *vm)
{
    // Default value when `frac == NaN` or `frac == inf`.
    int exp = 0;
    lulu_Number x = lulu_check_number(vm, 1);
    lulu_Number frac = frexp(x, &exp);
    lulu_push_number(vm, frac);
    lulu_push_integer(vm, exp);
    return 1;
}

static int
math_ldexp(lulu_VM *vm)
{
    lulu_Number mant = lulu_check_number(vm, 1);
    lulu_Integer exp = lulu_check_integer(vm, 2);
    lulu_Number res = ldexp(mant, exp);
    lulu_push_number(vm, res);
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

LULU_API int
lulu_open_math(lulu_VM *vm)
{
    lulu_set_library(vm, LULU_MATH_LIB_NAME, math_lib);

    /* constants */
    lulu_push_number(vm, M_E);
    lulu_set_field(vm, -2, "e");

    lulu_push_number(vm, M_PI);
    lulu_set_field(vm, -2, "pi");

    lulu_push_number(vm, 2*M_PI);
    lulu_set_field(vm, -2, "tau");

    lulu_push_number(vm, INFINITY);
    lulu_set_field(vm, -2, "inf");

    lulu_push_number(vm, NAN);
    lulu_set_field(vm, -2, "nan");

    lulu_push_number(vm, HUGE_VAL);
    lulu_set_field(vm, -2, "huge");
    return 1;
}
