package lulu

import "core:fmt"

// === VM MANIPULATION ======================================================{{{


@(require_results)
open :: proc(allocator := context.allocator) -> ^VM {
    @(static)
    vm: VM
    return &vm if vm_init(&vm, allocator) else nil
}

close :: proc(vm: ^VM) {
    vm_destroy(vm)
}

// }}}==========================================================================


/*
Notes:
-   Assumes the value we want to set with is at the very top of the stack.
    We will pop this value afterwards.
 */
set_global :: proc(vm: ^VM, key: string) {
    vkey := value_make_string(ostring_new(vm, key))
    table_set(vm, &vm.globals, vkey, vm.top[-1])
    pop(vm, 1)
}

get_global :: proc(vm: ^VM, key: string) {
    vkey  := value_make_string(ostring_new(vm, key))
    value := table_get(&vm.globals, vkey) or_else value_make_nil()
    push(vm, value)
}

to_string :: proc(vm: ^VM, index: int) -> (result: string, ok: bool) {
    value := index_to_address(vm, index)
    if !value_is_string(value^) do return "", false
    return ostring_to_string(value.ostring), true
}

concat :: proc(vm: ^VM, count: int) {
    switch count {
    case 0:
        push_string(vm, "")
        return
    case 1:
        // Would be redundant to pop the one string then re-push it!
        return
    }

    // Overwrite the first argument when done and do not pop it.
    target: [^]Value = &vm.top[-count]
    vm_concat(vm, target, target[:count])
    pop(vm, count - 1)
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
    // If negative we will index relative to the top
    from := vm.base if index >= 0 else vm.top
    return &from[index]
}

@(private="package")
push :: proc(vm: ^VM, value: Value) {
    vm.top = &vm.top[1]
    vm.top[-1] = value
}

pop :: proc(vm: ^VM, count: int) {
    // vm.top -= count
    vm.top = &vm.top[-count]
}
