#include "string.hpp"
#include "vm.hpp"

const char *const mt_names[MT_COUNT] = {
    "__index",      // MT_INDEX
    "__newindex",   // MT_NEWINDEX
    "__add",        // MT_ADD
    "__sub",        // MT_SUB
    "__mul",        // MT_MUL
    "__div",        // MT_DIV
    "__mod",        // MT_MOD
    "__pow",        // MT_POW
    "__unm",        // MT_UNM
    "__eq",         // MT_EQ
    "__lt",         // MT_LT
    "__leq",        // MT_LEQ
};

Value
mt_get_method(lulu_VM *L, Value v, Metamethod t)
{
    Table *mt = nullptr;
    switch (v.type()) {
    case VALUE_TABLE:
        mt = v.to_table()->metatable;
        break;
    default:
        mt = G(L)->mt_basic[v.type()];
        break;
    }
    if (mt == nullptr) {
        return nil;
    }
    OString *k = G(L)->mt_names[t];
    return table_get_string(mt, k);
}
