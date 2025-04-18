#+private
package lulu

import "core:math"
import "core:mem"
import "core:slice"

/*
**Overview**
-   Our version of Odin's `[dynamic]T` that does NOT track its own allocator,
    as we will use the VM's.
 */
DyArray :: struct($T: typeid) {
    data: []T,  // len(data) == cap
    len:    int,
}

dyarray_delete :: proc(vm: ^VM, dyarray: ^DyArray($T)) {
    delete(dyarray.data, vm.allocator)
    dyarray.data = {}
    dyarray.len  = 0
}

dyarray_len :: proc(dyarray: DyArray($T)) -> int {
    return dyarray.len
}

dyarray_cap :: proc(dyarray: DyArray($T)) -> int {
    return len(dyarray.data)
}

dyarray_data :: proc(dyarray: DyArray($T)) -> []T {
    return dyarray.data
}

dyarray_append :: proc(vm: ^VM, dyarray: ^DyArray($T), elem: T) {
    defer dyarray.len += 1
    old_len := dyarray_len(dyarray^)
    if old_len >= dyarray_cap(dyarray^) {
        old_data      := dyarray.data
        new_cap       := max(8, math.next_power_of_two(old_len + 1))
        new_data, err := make([]T, new_cap, vm.allocator)
        defer delete(old_data, vm.allocator)
        if err != nil {
            vm_memory_error(vm)
        }
        copy(new_data, old_data)
        dyarray.data = new_data
    }
    dyarray.data[old_len] = elem
}


dyarray_slice :: proc(dyarray: ^DyArray($T)) -> []T {
    return dyarray.data[:dyarray.len]
}
