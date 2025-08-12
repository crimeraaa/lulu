#+private
package lulu

import "base:builtin"
import "base:intrinsics"
import "core:math"
import "core:slice"

slice_make :: proc(vm: ^VM, $T: typeid, count: int) -> []T {
    s := make([]T, count, vm.allocator)
    if s == nil {
        vm_memory_error(vm)
    }
    return s
}

slice_insert :: proc(vm: ^VM, s: ^$S/[]$T, index: int, value: T) {
    if index >= len(s) {
        new_count := max(8, math.next_power_of_two(index + 1))
        slice_resize(vm, s, new_count)
    }
    s[index] = value
}

slice_resize :: proc(vm: ^VM, s: ^$S/[]$T, count: int) {
    // Nothing to do?
    if count == len(s) {
        return
    }
    prev := s^
    next := slice_make(vm, T, count)
    copy(next, prev)
    delete(prev, vm.allocator)
    s^ = next
}

slice_delete :: proc(vm: ^VM, s: ^$S/[]$T) {
    delete(s^, vm.allocator)
    s^ = {}
}

slice_offset :: proc(s: $S/[]$T, count: int) -> [^]T {
    return ptr_offset(raw_data(s), count)
}


ptr_offset :: intrinsics.ptr_offset
ptr_sub    :: intrinsics.ptr_sub


/*
**Overview**
-   Get the absolute index of `ptr` in `data`.
-   This is a VERY unsafe function as it makes many major assumptions.

**Assumptions**
-   `ptr` IS in range of `data[:]`.
-   This function NEVER returns an invalid index.
 */
ptr_index :: #force_inline proc "contextless" (ptr: ^$T, data: $S/[]T) -> (i: int) {
    return ptr_sub(cast([^]T)ptr, raw_data(data))
}


/*
**Overview**
-   Get the absolute index of `ptr` in `data`.
-   Checks first if `ptr` is indeed an element in range of `data`.
 */
ptr_index_safe :: proc "contextless" (ptr: ^$T, data: $S/[]T) -> (i: int, found: bool) {
    base_addr := uintptr(raw_data(data))
    end_addr  := uintptr(ptr_offset(raw_data(data), len(data)))
    if addr := uintptr(ptr); base_addr <= addr && addr < end_addr {
        return ptr_index(ptr, data), true
    }
    return -1, false
}
