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

void luluFun_init_chunk(Chunk *c, String *name)
{
    luluTbl_init(&c->mappings);
    luluVal_init_array(&c->constants);
    c->name     = name;
    c->code     = nullptr;
    c->lines    = nullptr;
    c->length   = 0;
    c->capacity = 0;
}

void luluFun_free_chunk(lulu_VM *vm, Chunk *c)
{
    luluTbl_free(vm, &c->mappings);
    luluVal_free_array(vm, &c->constants);
    luluMem_free_parray(vm, c->lines, c->length);
    luluMem_free_parray(vm, c->code, c->length);
    luluFun_init_chunk(c, nullptr);
}

void luluFun_write_chunk(lulu_VM *vm, Chunk *c, Byte data, int line)
{
    if (c->length + 1 > c->capacity) {
        int prev    = c->capacity;
        int next    = luluMem_grow_capacity(prev);
        c->code     = luluMem_resize_parray(vm, c->code,  prev, next);
        c->lines    = luluMem_resize_parray(vm, c->lines, prev, next);
        c->capacity = next;
    }
    c->code[c->length]  = data;
    c->lines[c->length] = line;
    c->length += 1;
}

int luluFun_add_constant(lulu_VM *vm, Chunk *c, const Value *v)
{
    Value  i;
    Table *t = &c->mappings;
    Array *k = &c->constants;
    // Need to create new mapping?
    if (!luluTbl_get(t, v, &i)) {
        luluVal_write_array(vm, k, v);
        setv_number(&i, k->length - 1); // `int` should fit in `lulu_Number`
        luluTbl_set(vm, t, v, &i);
    }
    return cast_int(as_number(&i));
}
