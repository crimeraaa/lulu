#include "lulu_auxlib.h"
#include "private.hpp"

LULU_API int
lulu_errorf(lulu_VM *vm, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    lulu_push_vfstring(vm, fmt, args);
    va_end(args);
    return lulu_error(vm);
}

void
lulu_set_library(lulu_VM *vm, const char *libname,
    const lulu_Register *library, size_t n)
{
    if (libname == nullptr) {
        lulu_push_value(vm, LULU_GLOBALS_INDEX);
    } else {
        lulu_get_global(vm, libname);
        // _G[libname] doesn't exist yet?
        if (lulu_is_nil(vm, -1)) {
            // Remove the `nil` result from `lulu_get_global()`.
            lulu_pop(vm, 1);

            // Do `_G[libname] = {}`.
            lulu_new_table(vm, 0, cast_int(n));
            lulu_push_value(vm, -1);
            lulu_set_global(vm, libname);
        }
    }
    for (size_t i = 0; i < n; i++) {
        // TODO(2025-07-01): Ensure key and value are not collected!
        lulu_push_cfunction(vm, library[i].function);
        lulu_set_field(vm, -2, library[i].name);
    }
    lulu_pop(vm, 1);
}
