#pragma once

#include "private.hpp"

#define GC_HEAP_GROW_FACTOR     2

enum GC_Factor {
    GC_KILOBYTE_EXP = 10, // 2^10 = 1024 bytes (0x400)
    GC_MEGABYTE_EXP = 20, // 2^20 = 1_048_576 bytes (0x100_000)

    GC_KILOBYTE = 1 << GC_KILOBYTE_EXP,
    GC_MEGABYTE = GC_KILOBYTE * GC_KILOBYTE,
};

#define GC_THRESHOLD_INIT   GC_KILOBYTE

enum GC_State : u8 {
    GC_PAUSED,
    GC_MARK,
    GC_TRACE,
    GC_SWEEP,
};

// Defined in vm.hpp.
struct lulu_Global;

// Defined in compiler.hpp.
struct Compiler;

/**
 * @note(2025-08-27)
 *      Analogous to `memory.c:markRoots()` in Crafting Interpreters 26.3:
 *      Marking the Roots.
 */
void
gc_collect_garbage(lulu_VM *L, lulu_Global *g);


/** @brief Start a collection if GC threshold is surpassed.
 *
 * @note(2025-09-01)
 *  Analogous to `lgc.h:luaC_checkGC()` in Lua 5.1.5.
 */
void
gc_check(lulu_VM *L, lulu_Global *g);
