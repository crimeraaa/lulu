#include <time.h>
#include "api.h"
#include "baselib.h"
#include "common.h"
#include "object.h"
#include "value.h"

static TValue base_clock(LVM *vm, int argc) {
    unused2(vm, argc);
    return makenumber((lua_Number)clock() / CLOCKS_PER_SEC);
}

static TValue base_print(LVM *vm, int argc) {
    unused(vm);
    for (int i = 0; i < argc; i++) {
        printf("%s\t", lua_tostring(vm, i));
    }
    printf("\n");
    return makenil;
}

static TValue base_type(LVM *vm, int argc) {
    if (argc == 0) {
        lua_argerror(vm, 1, "type", NULL, NULL);
    }
    const TNameInfo *tname = get_tnameinfo(lua_type(vm, 0));
    return makestring(copy_string(vm, tname->what, tname->len));
}

static TValue base_dumptable(LVM *vm, int argc) {
    if (argc == 0) {
        lua_argerror(vm, 1, "dumptable", NULL, NULL);
    } else if (!lua_istable(vm, 0)) {
        const char *typename = lua_typename(vm, lua_type(vm, 0));
        lua_argerror(vm, 1, "dumptable", "table", typename);
    }
    print_table(lua_astable(vm, 0), true);
    return makenil;
}

static const lua_Library baselib = {
    {"dumptable",   base_dumptable},
    {"clock",       base_clock},
    {"print",       base_print},
    {"type",        base_type},
    {NULL,          NULL},
};

void lua_loadbase(LVM *vm) {
    lua_loadlibrary(vm, "_G", baselib);
}
