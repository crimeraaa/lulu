#+private
package lulu

import "core:math"


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

dyarray_get :: proc(dyarray: DyArray($T), index: int) -> T {
    return dyarray.data[index]
}

dyarray_get_ptr :: proc(dyarray: ^DyArray($T), index: int) -> ^T {
    return &dyarray.data[index]
}

dyarray_get_safe :: proc(dyarray: DyArray($T), index: int) -> (value: T, ok: bool) {
    if 0 <= index && index < dyarray.len {
        return dyarray.data[index], true
    }
    return {}, false
}

dyarray_append :: proc(vm: ^VM, dyarray: ^DyArray($T), elem: T) {
    defer dyarray.len += 1
    old_len := dyarray_len(dyarray^)
    if old_len >= dyarray_cap(dyarray^) {
        dyarray_resize(vm, dyarray, max(8, math.next_power_of_two(old_len + 1)))
    }
    dyarray.data[old_len] = elem
}

dyarray_resize :: proc(vm: ^VM, dyarray: ^DyArray($T), new_cap: int) {
    old_data := dyarray.data
    defer delete(old_data, vm.allocator)
    new_data, err := make([]T, new_cap, vm.allocator)
    if err != nil {
        vm_memory_error(vm)
    }
    copy(new_data, old_data)
    dyarray.data = new_data
}

dyarray_slice :: proc(dyarray: ^DyArray($T), end_index := -1) -> []T {
    return dyarray.data[:dyarray.len if end_index == -1 else end_index]
}
