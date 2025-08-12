#include <time.h>

#include "lulu_auxlib.h"

#define cast(T)     (T)

static int
os_clock(lulu_VM *vm)
{
    lulu_Number now = cast(lulu_Number)clock();
    lulu_push_number(vm, now / cast(lulu_Number)CLOCKS_PER_SEC);
    return 1;
}

static const lulu_Register
os_lib[] = {
    {"clock", &os_clock},
};

LULU_API int
lulu_open_os(lulu_VM *vm)
{
    lulu_set_library(vm, LULU_OS_LIB_NAME, os_lib);
    return 1;
}
