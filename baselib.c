#include <time.h>
#include "api.h"
#include "baselib.h"
#include "common.h"
#include "object.h"
#include "value.h"

static const lua_Library baselib = {
    {"clock",   base_clock},
    {"print",   base_print},
    {"type",    base_type},
    {NULL,      NULL},
};

void lua_loadbase(LVM *vm) {
    lua_registerlib(vm, baselib);
}

TValue base_clock(LVM *vm, int argc, TValue *argv) {
    unused3(vm, argc, argv);
    return makenumber((lua_Number)clock() / CLOCKS_PER_SEC);
}

TValue base_print(LVM *vm, int argc, TValue *argv) {
    unused(vm);
    for (int i = 0; i < argc; i++) {
        print_value(&argv[i]);
        printf(" ");
    }
    printf("\n");
    return makenil;
}

TValue base_type(LVM *vm, int argc, TValue *argv) {
    if (argc != 1) {
        lua_error(vm, "'type' requires exactly one argument.");
    }
    const TNameInfo *tname = get_tnameinfo(argv[0].type);
    return makestring(copy_string(vm, tname->what, tname->len));
}
