#+private
package lulu

import "base:builtin"
import "core:math"

slice_make :: proc(vm: ^VM, $T: typeid, count: int) -> []T {
    s := make([]T, count, vm.allocator)
    if s == nil {
        vm_memory_error(vm)
    }
    return s
}

slice_insert :: proc(vm: ^VM, slice: ^$S/[]$T, index: int, value: T) {
    if index >= len(slice) {
        new_count := max(8, math.next_power_of_two(index + 1))
        tmp       := slice_make(vm, T, new_count)
        copy(tmp, slice^)
        delete(slice^, vm.allocator)
        slice^ = tmp
    }
    slice[index] = value
}

slice_delete :: proc(vm: ^VM, slice: ^$S/[]$T) {
    delete(slice^, vm.allocator)
    slice^ = {}
}
