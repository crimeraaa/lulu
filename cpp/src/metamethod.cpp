#include "string.hpp"
#include "vm.hpp"

const char *const mt_names[MT_COUNT] = {
    "__index",      // MT_INDEX
    "__newindex",   // MT_NEWINDEX
    "__eq",         // MT_EQ
    "__len",        // MT_LEN
    "__gc",         // MT_GC
    "__add",        // MT_ADD
    "__sub",        // MT_SUB
    "__mul",        // MT_MUL
    "__div",        // MT_DIV
    "__mod",        // MT_MOD
    "__pow",        // MT_POW
    "__unm",        // MT_UNM
    "__lt",         // MT_LT
    "__le",         // MT_LEQ
};

Value
mt_get_fast(lulu_VM *L, Table *mt, Metamethod m)
{
    if (mt == nullptr) {
        return nil;
    }

    // Flag bit set is optimized for absence of metamethods.
    if (mt->flags & (1u << m)) {
        return nil;
    }

    Value mf = table_get_string(mt, G(L)->mt_names[m]);
    lulu_assert(MT_INDEX <= m && m <= MT_GC);
    // Metamethod not found? Cache this finding.
    if (mf.is_nil()) {
        mt->flags |= (1u << m);
    }
    return mf;
}

Value
mt_get_method(lulu_VM *L, Value v, Metamethod t)
{
    Table *mt = nullptr;
    switch (v.type()) {
    case VALUE_TABLE:
        mt = v.to_table()->metatable;
        break;
    case VALUE_USERDATA:
        mt = v.to_userdata()->metatable;
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
