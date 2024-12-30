#ifndef LULU_DEBUG_H
#define LULU_DEBUG_H

#include "chunk.h"

LULU_ATTR_PRINTF(4, 5)
int
debug_writef(cstring level, cstring file, int line, cstring fmt, ...);

#define debug_writef(level, fmt, ...)  debug_writef(level, __FILE__, __LINE__, fmt "\n", __VA_ARGS__)
#define debug_fatalf(fmt, ...)         debug_writef("FATAL", fmt, __VA_ARGS__)
#define debug_fatal(msg)               debug_fatalf("%s", msg)

#ifdef LULU_DEBUG_ASSERT

#define debug_assert(cond, msg)                                                \
do {                                                                           \
    if (!(cond)) {                                                             \
        debug_fatalf("assertion '%s' failed: %s", #cond, msg);                 \
        __builtin_trap();                                                      \
    }                                                                          \
} while (0)

#else // !LULU_DEBUG_ASSERT

#define debug_assert(cond, msg)

#endif // LULU_DEBUG_ASSERT

void
debug_print_value(const Value *value);

void
debug_disasssemble_chunk(const Chunk *chunk);

int
debug_disassemble_instruction(const Chunk *chunk, int index);

#endif // LULU_DEBUG_H
