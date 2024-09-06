#ifndef LULU_DEBUG_H
#define LULU_DEBUG_H

#include "lulu.h"
#include "chunk.h"

#ifdef LULU_DEBUG_ASSERT

#include <stdio.h>

#define lulu_Debug_assert(cond, msg)                                           \
do {                                                                           \
    if (!(cond)) {                                                             \
        fprintf(stderr, "[FATAL] %s:%i: assertion '%s' failed: %s\n",          \
                __FILE__, __LINE__, #cond, msg);                               \
        fflush(stderr);                                                        \
        __builtin_trap();                                                      \
    }                                                                          \
} while (0)

#else // !LULU_DEBUG_ASSERT

#define lulu_Debug_assert(cond, msg)

#endif // LULU_DEBUG_ASSERT


void lulu_Debug_print_value(const lulu_Value *value);
void lulu_Debug_disasssemble_chunk(const lulu_Chunk *chunk, cstring name);
isize lulu_Debug_disassemble_instruction(const lulu_Chunk *chunk, isize index);

#endif // LULU_DEBUG_H
