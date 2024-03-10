#include <time.h>
#include "api.h"
#include "baselib.h"
#include "common.h"
#include "object.h"
#include "value.h"

static const RegFunc baselib[] = {
    {"clock",   base_clock},
    {"print",   base_print},
    {"type",    base_type},
};

void lua_loadbaselib(LVM *vm) {
    for (size_t i = 0; i < arraylen(baselib); i++) {
        const RegFunc *rfunc = &baselib[i];
        lua_pushliteral(vm, rfunc->name);   // Stack index 0
        lua_pushcfunction(vm, rfunc->func); // Stack index 1
        table_set(&vm->globals, asstring(&vm->stack[0]), vm->stack[1]);
        lua_popn(vm, 2);
    }
}

TValue base_clock(LVM *vm, int argc, TValue *argv) {
    (void)vm; (void)argc; (void)argv;
    return makenumber((lua_Number)clock() / CLOCKS_PER_SEC);
}

TValue base_print(LVM *vm, int argc, TValue *argv) {
    (void)vm;
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
