#include "chunk.h"
#include "object.h"
#include "memory.h"

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
    [OP_RETURN]     = {"RETURN",        0,          0,          0},
};

static_assert(array_len(LULU_OPINFO) == NUM_OPCODES, "Bad opcode count");

void init_chunk(Chunk *ck, const char *name)
{
    init_varray(&ck->constants);
    ck->name  = name;
    ck->code  = NULL;
    ck->lines = NULL;
    ck->len   = 0;
    ck->cap   = 0;
}

void free_chunk(Chunk *ck, Alloc *al)
{
    free_varray(&ck->constants, al);
    free_parray(ck->lines, ck->len, al);
    free_parray(ck->code, ck->len, al);
    init_chunk(ck, "(freed chunk)");
}

void write_chunk(Chunk *ck, Byte data, int line, Alloc *al)
{
    if (ck->len + 1 > ck->cap) {
        int prev  = ck->cap;
        int next  = grow_capacity(prev);
        ck->code  = resize_parray(ck->code,  prev, next, al);
        ck->lines = resize_parray(ck->lines, prev, next, al);
        ck->cap   = next;
    }
    ck->code[ck->len]  = data;
    ck->lines[ck->len] = line;
    ck->len++;
}

int add_constant(Chunk *ck, const Value *vl, Alloc *al)
{
    VArray *kst = &ck->constants;
    // TODO: Literally anything is faster than al linear search
    for (int i = 0; i < kst->len; i++) {
        if (values_equal(&kst->values[i], vl)) {
            return i;
        }
    }
    write_varray(kst, vl, al);
    return kst->len - 1;
}
