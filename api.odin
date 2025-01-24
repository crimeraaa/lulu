package lulu

import "core:fmt"

to_string :: proc(vm: ^VM, index: int) -> (result: string, ok: bool) {
    value := index_to_address(vm, index)
    if !value_is_string(value^) {
        return "", false
    }
    return ostring_to_string(value.ostring), true
}

concat :: proc(vm: ^VM, count: int) {
    switch count {
    case 0: push_string(vm, ""); return
    case 1: // Would be redundant to pop the one string then re-push it!
    }
    dst := &Value{}
    vm_concat(vm, dst, vm.stack[vm.top - count:vm.top])
    pop(vm, count)
    push(vm, dst^)
}

push_string :: proc(vm: ^VM, str: string) -> (result: string) {
    interned := ostring_new(vm, str)
    push(vm, value_make_string(interned))
    return ostring_to_string(interned)
}

push_fstring :: proc(vm: ^VM, format: string, args: ..any) -> (result: string) {
    builder := vm_get_builder(vm)
    return push_string(vm, fmt.sbprintf(builder, format, ..args))
}

// You may use negative indexes to resolve from the top.
@(private="file")
index_to_address :: proc(vm: ^VM, index: int) -> ^Value {
    from := vm.base if index >= 0 else vm.top
    return &vm.stack[from + index]
}

@(private="file")
push :: proc(vm: ^VM, value: Value) {
    vm.stack[vm.top] = value
    vm.top += 1
}

pop :: proc(vm: ^VM, count: int) {
    vm.top -= count
}
