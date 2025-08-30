#pragma once

#include "private.hpp"

#define GC_HEAP_GROW_FACTOR     2

// 1 kilobyte == 1024 bytes.
#define GC_THRESHOLD_INIT       1024

// 1 megabyte == 1024 kilobytes == 1_048_576 bytes.
// #define GC_THRESHOLD_INIT   (1024 * 1024)

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


/**
 * @note(2025-08-27)
 *      Analogous to `memory.c:markCompilerRoots()` in Crafting Interpreters
 *      26.3.1: Less obvious roots.
 */
void
gc_mark_compiler_roots(lulu_VM *L, Compiler *c);


