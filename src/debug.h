#ifndef LULU_DEBUG_H
#define LULU_DEBUG_H

#include "chunk.h"

LULU_ATTR_PRINTF(4, 5)
int
lulu_Debug_writef(cstring level, cstring file, int line, cstring fmt, ...);

#define lulu_Debug_writef(level, fmt, ...)  lulu_Debug_writef(level, __FILE__, __LINE__, fmt "\n", __VA_ARGS__)
#define lulu_Debug_fatalf(fmt, ...)         lulu_Debug_writef("FATAL", fmt, __VA_ARGS__)
#define lulu_Debug_fatal(msg)               lulu_Debug_fatalf("%s", msg)

#ifdef LULU_DEBUG_ASSERT

#define lulu_Debug_assert(cond, msg)                                           \
do {                                                                           \
    if (!(cond)) {                                                             \
        lulu_Debug_fatalf("assertion '%s' failed: %s", #cond, msg);            \
        __builtin_trap();                                                      \
    }                                                                          \
} while (0)

#else // !LULU_DEBUG_ASSERT

#define lulu_Debug_assert(cond, msg)

#endif // LULU_DEBUG_ASSERT

void
lulu_Debug_print_value(const lulu_Value *value);

void
lulu_Debug_disasssemble_chunk(const lulu_Chunk *chunk);

isize
lulu_Debug_disassemble_instruction(const lulu_Chunk *chunk, isize index);

#endif // LULU_DEBUG_H
