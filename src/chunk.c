#include "chunk.h"

// @todo 2024-09-22 Just remove the designated initializers entirely!
#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wc99-designator"
#endif

const OpCode_Info
LULU_OPCODE_INFO[LULU_OPCODE_COUNT] = {
    //                name: cstring     sz_arg,     n_push,     n_pop: i8
    [OP_CONSTANT]   = {"CONSTANT",      3,           1,          0},
    [OP_GET_GLOBAL] = {"GETGLOBAL",     3,           1,          0},
    [OP_SET_GLOBAL] = {"SETGLOBAL",     3,           0,          1},
    [OP_GET_LOCAL]  = {"GETLOCAL",      1,           1,          0},
    [OP_SET_LOCAL]  = {"SETLOCAL",      1,           0,          1},
    [OP_NEW_TABLE]  = {"NEWTABLE",      3,           1,          0},
    [OP_GET_TABLE]  = {"GETTABLE",      0,           1,          2}, // @todo 2024-12-27: Add argument for pop?
    [OP_SET_TABLE]  = {"SETTABLE",      3,           0,         -1},
    [OP_LEN]        = {"LEN",           0,           0,          0},
    [OP_NIL]        = {"NIL",           1,          -1,          0},
    [OP_TRUE]       = {"TRUE",          0,           1,          0},
    [OP_FALSE]      = {"FALSE",         0,           1,          0},
    [OP_ADD]        = {"ADD",           0,           0,          1},
    [OP_SUB]        = {"SUB",           0,           0,          1},
    [OP_MUL]        = {"MUL",           0,           0,          1},
    [OP_DIV]        = {"DIV",           0,           0,          1},
    [OP_MOD]        = {"MOD",           0,           0,          1},
    [OP_POW]        = {"POW",           0,           0,          1},
    [OP_CONCAT]     = {"CONCAT",        1,           1,         -1},
    [OP_UNM]        = {"UNM",           0,           0,          0},
    [OP_EQ]         = {"EQ",            0,           0,          1},
    [OP_LT]         = {"LT",            0,           0,          1},
    [OP_LEQ]        = {"LEQ",           0,           0,          1},
    [OP_NOT]        = {"NOT",           0,           0,          0},
    [OP_PRINT]      = {"PRINT",         1,           0,         -1},
    [OP_POP]        = {"POP",           1,           0,         -1},
    [OP_RETURN]     = {"RETURN",        0,           0,          0},
};

#if defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

void
chunk_init(Chunk *self, cstring filename)
{
    varray_init(&self->constants);
    self->code     = NULL;
    self->lines    = NULL;
    self->filename = filename;
    self->len      = 0;
    self->cap      = 0;
}

void
chunk_write(lulu_VM *vm, Chunk *self, Instruction inst, int line)
{
    // Appending to this index would cause a buffer overrun?
    int index = self->len++;
    if (index >= self->cap) {
        chunk_reserve(vm, self, mem_grow_capacity(self->cap));
    }
    self->code[index]  = inst;
    self->lines[index] = line;
}

void
chunk_reserve(lulu_VM *vm, Chunk *self, int new_cap)
{
    int old_cap = self->cap;
    // Nothing to do?
    if (new_cap <= old_cap) {
        return;
    }

    self->code  = array_resize(Instruction, vm, self->code, old_cap, new_cap);
    self->lines = array_resize(int, vm, self->lines, old_cap, new_cap);
    self->cap   = new_cap;
}

void
chunk_free(lulu_VM *vm, Chunk *self)
{
    varray_free(vm, &self->constants);
    array_free(Instruction, vm, self->code, self->cap);
    array_free(int, vm, self->lines, self->cap);
    chunk_init(self, "(freed chunk)");
}

int
chunk_add_constant(lulu_VM *vm, Chunk *self, const Value *value)
{
    VArray *constants = &self->constants;

    /**
     * @note 2024-12-10
     *      Theoretically VERY inefficient, but works for the general case where
     *      there are not that many constants.
     */
    for (int i = 0; i < constants->len; i++) {
        if (value_eq(value, &constants->values[i])) {
            return i;
        }
    }
    varray_append(vm, constants, value);
    return constants->len - 1;
}
