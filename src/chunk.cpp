#include <stdio.h>

#include "chunk.hpp"

void
chunk_append(lulu_VM &vm, Chunk &c, Instruction i)
{
    dynamic_push(vm, c.code, i);
}

u32
chunk_add_constant(lulu_VM &vm, Chunk &c, Value v)
{
    for (auto &v2 : c.constants) {
        if (v2 == v) {
            return u32(&v2 - &c.constants[0]);
        }
    }
    dynamic_push(vm, c.constants, v);
    return u32(c.constants.m_len - 1);
}

void
chunk_destroy(lulu_VM &vm, Chunk &c)
{
    dynamic_destroy(vm, c.code);
}

static const char *
opcode_names[] = {
    "Load_Constant",
    "Add",
    "Sub",
    "Mul",
    "Div",
    "Mod",
    "Pow",
    "Return",
};

void
chunk_list(const Chunk &c)
{
    printf("=== DISASSEMBLY BEGIN ===\n");
    if (c.constants.m_len > 0) {
        printf("\n.const:\n");
        for (auto &v : c.constants) {
            printf("[%ti] %f\n", &v - &c.constants[0], v);
        }
        printf("\n");
    }
    printf(".code:\n");
    for (auto &i : c.code) {
        printf("[%ti] %s", &i - &c.code[0], opcode_names[i.op()]);
        switch (i.op()) {
        case OP_LOAD_CONSTANT:
            printf("%i %i\n", i.a(), i.bx());
            break;
        case OP_ADD: case OP_SUB:
        case OP_MUL: case OP_DIV:
        case OP_MOD: case OP_POW:
            printf("%i %i %i\n", i.a(), i.b(), i.c());
            break;
        case OP_RETURN:
            printf("Return\n");
            break;
        }
    }
    printf("\n=== DISASSEMBLY END ===\n");
}
