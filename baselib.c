#include <time.h>
#include "api.h"
#include "baselib.h"
#include "common.h"
#include "object.h"
#include "value.h"

static TValue base_clock(LVM *vm, int argc, TValue *argv) {
    unused3(vm, argc, argv);
    return makenumber((lua_Number)clock() / CLOCKS_PER_SEC);
}

static TValue base_print(LVM *vm, int argc, TValue *argv) {
    unused(vm);
    for (int i = 0; i < argc; i++) {
        print_value(&argv[i]);
        printf("\t");
    }
    printf("\n");
    return makenil;
}

static TValue base_type(LVM *vm, int argc, TValue *argv) {
    if (argc != 1) {
        lua_error(vm, "'type' expects exactly 1 argument.");
    }
    const TNameInfo *tname = get_tnameinfo(argv[0].type);
    return makestring(copy_string(vm, tname->what, tname->len));
}

static TValue base_dumptable(LVM *vm, int argc, TValue *argv) {
    if (argc != 1 || !istable(&argv[0])) {
        lua_error(vm, "'dumptable' expects exactly 1 argument of type 'table'.");
    } 
    const Table *table = astable(&argv[0]);
    print_table(table, true);
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
    lua_registerlib(vm, baselib);
}
