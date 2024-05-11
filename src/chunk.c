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
    [OP_GETLOCAL]   = {"GETLOCAL",      1,          1,          0},
    [OP_GETGLOBAL]  = {"GETGLOBAL",     3,          1,          0},
    [OP_GETTABLE]   = {"GETTABLE",      0,          1,          2},
    [OP_SETLOCAL]   = {"SETLOCAL",      1,          0,          1},
    [OP_SETGLOBAL]  = {"SETGLOBAL",     3,          0,          1},
    [OP_SETTABLE]   = {"SETTABLE",      2,          0,          VAR_DELTA},
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

void init_chunk(Chunk *self, const char *name)
{
    init_varray(&self->constants);
    self->name  = name;
    self->code  = NULL;
    self->lines = NULL;
    self->len   = 0;
    self->cap   = 0;
}

void free_chunk(Chunk *self, Alloc *alloc)
{
    free_varray(&self->constants, alloc);
    free_array(Byte, self->code,  self->len, alloc);
    free_array(int,  self->lines, self->len, alloc);
    init_chunk(self, "(freed chunk)");
}

void write_chunk(Chunk *self, Byte data, int line, Alloc *alloc)
{
    if (self->len + 1 > self->cap) {
        int prev    = self->cap;
        int next    = grow_capacity(prev);
        self->code  = resize_array(Byte, self->code,  prev, next, alloc);
        self->lines = resize_array(int,  self->lines, prev, next, alloc);
        self->cap   = next;
    }
    self->code[self->len]  = data;
    self->lines[self->len] = line;
    self->len++;
}

int add_constant(Chunk *self, const Value *value, Alloc *alloc)
{
    VArray *constants = &self->constants;
    // TODO: Literally anything is faster than a linear search
    for (int i = 0; i < constants->len; i++) {
        if (values_equal(&constants->values[i], value)) {
            return i;
        }
    }
    write_varray(constants, value, alloc);
    return constants->len - 1;
}
