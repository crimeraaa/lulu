#include "string.hpp"
#include "vm.hpp"

const char *const mt_names[MT_COUNT] = {
    "__add", // MT_ADD
    "__sub", // MT_SUB
    "__mul", // MT_MUL
    "__div", // MT_DIV
    "__mod", // MT_MOD
    "__pow", // MT_POW
    "__unm", // MT_UNM
    "__eq",  // MT_EQ
    "__lt",  // MT_LT
    "__leq", // MT_LEQ
};

Value
mt_get_metatable(lulu_VM *L, Value v, Metamethod metamethod)
{
    Table *metatable = nullptr;
    switch (v.type()) {
    case VALUE_TABLE:
        metatable = v.to_table()->metatable;
        break;
    default:
        break;
    }
    if (metatable == nullptr) {
        return nil;
    }
    return table_get_string(metatable, G(L)->mt_names[metamethod]);
}
