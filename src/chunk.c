#include "chunk.h"
#include "object.h"
#include "memory.h"
#include "table.h"

OpInfo LULU_OPINFO[] = {
    // OPCODE           NAME            ARGSZ       #PUSH       #POP
    [OP_CONSTANT]   = {"CONSTANT",      3,          1,          0},
    [OP_NIL]        = {"NIL",           1,          VAR_DELTA,  0},
    [OP_TRUE]       = {"TRUE",          0,          1,          0},
    [OP_FALSE]      = {"FALSE",         0,          1,          0},
    [OP_POP]        = {"POP",           1,          0,          VAR_DELTA},
    [OP_NEWTABLE]   = {"NEWTABLE",      3,          1,          0},
    [OP_GETLOCAL]   = {"GETLOCAL",      1,          1,          0},
    [OP_GETGLOBAL]  = {"GETGLOBAL",     3,          1,          0},
    [OP_GETTABLE]   = {"GETTABLE",      0,          1,          2},
    [OP_SETLOCAL]   = {"SETLOCAL",      1,          0,          1},
    [OP_SETGLOBAL]  = {"SETGLOBAL",     3,          0,          1},
    [OP_SETTABLE]   = {"SETTABLE",      3,          0,          VAR_DELTA},
    [OP_SETARRAY]   = {"SETARRAY",      2,          0,          VAR_DELTA},
    [OP_EQ]         = {"EQ",            0,          1,          2},
    [OP_LT]         = {"LT",            0,          1,          2},
    [OP_LE]         = {"LE",            0,          1,          2},
    [OP_ADD]        = {"ADD",           0,          1,          2},
    [OP_SUB]        = {"SUB",           0,          1,          2},
    [OP_MUL]        = {"MUL",           0,          1,          2},
    [OP_DIV]        = {"DIV",           0,          1,          2},
    [OP_MOD]        = {"MOD",           0,          1,          2},
    [OP_POW]        = {"POW",           0,          1,          2},
    [OP_CONCAT]     = {"CONCAT",        1,          1,          VAR_DELTA},
    [OP_UNM]        = {"UNM",           0,          1,          1},
    [OP_NOT]        = {"NOT",           0,          1,          1},
    [OP_LEN]        = {"LEN",           0,          1,          1},
    [OP_PRINT]      = {"PRINT",         1,          0,          VAR_DELTA},
    [OP_TEST]       = {"TEST",          0,          0,          0},
    [OP_JUMP]       = {"JUMP",          3,          0,          0},
    [OP_FORPREP]    = {"FORPREP",       0,          1,          0},
    [OP_FORLOOP]    = {"FORLOOP",       0,          0,          0},
    [OP_RETURN]     = {"RETURN",        0,          0,          0},
};

static_assert(array_len(LULU_OPINFO) == NUM_OPCODES, "Bad opcode count");

void luluFun_init_chunk(Chunk *ck, const char *name)
{
    luluTbl_init(&ck->mappings);
    luluVal_init_array(&ck->constants);
    ck->name  = name;
    ck->code  = NULL;
    ck->lines = NULL;
    ck->len   = 0;
    ck->cap   = 0;
}

void luluFun_free_chunk(lulu_VM *vm, Chunk *ck)
{
    luluTbl_free(vm, &ck->mappings);
    luluVal_free_array(vm, &ck->constants);
    luluMem_free_parray(vm, ck->lines, ck->len);
    luluMem_free_parray(vm, ck->code, ck->len);
    luluFun_init_chunk(ck, "(freed chunk)");
}

void luluFun_write_chunk(lulu_VM *vm, Chunk *ck, Byte data, int line)
{
    if (ck->len + 1 > ck->cap) {
        int prev  = ck->cap;
        int next  = luluMem_grow_capacity(prev);
        ck->code  = luluMem_resize_parray(vm, ck->code,  prev, next);
        ck->lines = luluMem_resize_parray(vm, ck->lines, prev, next);
        ck->cap   = next;
    }
    ck->code[ck->len]  = data;
    ck->lines[ck->len] = line;
    ck->len++;
}

int luluFun_add_constant(lulu_VM *vm, Chunk *ck, const Value *val)
{
    Value  i;
    Table *t = &ck->mappings;
    Array *k = &ck->constants;
    // Need to create new mapping?
    if (!luluTbl_get(t, val, &i)) {
        luluVal_write_array(vm, k, val);
        setv_number(&i, k->len - 1); // `int` should fit in `lulu_Number`
        luluTbl_set(vm, t, val, &i);
    }
    return cast_int(as_number(&i));
}
