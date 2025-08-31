#include "mem.hpp"
#include "vm.hpp"
#include "compiler.hpp"

#define REPEAT_2(n)   n, n
#define REPEAT_4(n)   REPEAT_2(n), REPEAT_2(n)
#define REPEAT_8(n)   REPEAT_4(n), REPEAT_4(n)
#define REPEAT_16(n)  REPEAT_8(n), REPEAT_8(n)
#define REPEAT_32(n)  REPEAT_16(n), REPEAT_16(n)
#define REPEAT_64(n)  REPEAT_32(n), REPEAT_32(n)
#define REPEAT_128(n) REPEAT_64(n), REPEAT_64(n)

int
mem_ceil_log2(usize n)
{
    lulu_assume(n > 0);

    /**
     * @brief
     *      Map indices in the range [1, 256] to the index range exponents
     *      with which they fit at the end. Useful to determine
     *      the appropriate array size which `n` fits in.
     *
     * @note(2025-08-14)
     *      Index 0 is never a valid input, so this table actually
     *      maps `n - 1`.
     */
    static const u8 ceil_log2_lookup_table[0x100] = {
        // [1, 1] => index_ranges[0]
        0,

        // [2, 2] => index_ranges[1]
        1,

        // [3, 4] => index_ranges[2]
        // Index 3 should map to bit 2 as given by ceil(log2(3)), NOT bit 1
        // as given by floor(log2(3)). Because when we calculate the optimal
        // array size, we want size of 4, not size of 2.
        REPEAT_2(2),
        REPEAT_4(3),   // [5, 8]
        REPEAT_8(4),   // [9, 16]
        REPEAT_16(5),  // [17, 32]
        REPEAT_32(6),  // [33, 64]
        REPEAT_64(7),  // [65, 128]
        REPEAT_128(8), // [129, 256]
    };

    // Accumulator for values of n that do not fit in the lookup table.
    // We know that if it does not fit, 2^8 is automatically added on top.
    // Concept check: ceil(log2(257))
    int acc = 0;
    while (n > 0x100) {
        n >>= 8;
        acc += 8;
    }
    return acc + ceil_log2_lookup_table[n - 1];
}

void *
mem_rawrealloc(lulu_VM *L, void *ptr, usize old_size, usize new_size)
{
    lulu_Global *g = G(L);

    // Allocating a new block, or resizing an old one?
    if (new_size > old_size) {
        g->n_bytes_allocated += new_size - old_size;
#ifdef LULU_DEBUG_STRESS_GC
        gc_collect_garbage(L, g);
#else
        // GC is 'paused' if threshold == USIZE_MAX, because we assume we will
        // never be able to validly acquire that much memory.
        if (g->n_bytes_allocated > g->gc_threshold) {
            gc_collect_garbage(L, g);
        }
#endif
    }
    // Shrinking or freeing an existing block?
    else {
        g->n_bytes_allocated -= old_size - new_size;
    }

    void *next = g->allocator(g->allocator_data, ptr, old_size, new_size);
    // Allocation request, that wasn't attempting to free, failed?
    if (next == nullptr && new_size != 0) {
        vm_throw(L, LULU_ERROR_MEMORY);
    }
    return next;
}

