package lulu

import "core:fmt"

///=== VM MANIPULATION ===================================================== {{{


@(require_results)
open :: proc(allocator := context.allocator) -> ^VM {
    @(static)
    vm: VM
    return &vm if vm_init(&vm, allocator) else nil
}

close :: proc(vm: ^VM) {
    vm_destroy(vm)
}

///=== }}} =====================================================================


/*
**Overview**
-   Get the index of the current stack top.
-   This also represents the size of the current stack frame.

**Analogous to**
-   `int lua_gettop(lua_State *L)`.
 */
get_top :: proc(vm: ^VM) -> (index: int) {
    return ptr_sub(vm.top, vm.base)
}


/*
**Notes**
-   Assumes the value we want to set with is at the very top of the stack,
    a.k.a. at index `-1`.
-   We will pop this value afterwards.
 */
set_global :: proc(vm: ^VM, key: string) {
    vkey := value_make(ostring_new(vm, key))
    table_set(vm, &vm.globals, vkey, vm.top[-1])
    pop(vm, 1)
}


/* 
**Notes**
-   See the notes regarding the stack in `push_rawvalue()`.
 */
get_global :: proc(vm: ^VM, key: string) {
    vkey  := value_make(ostring_new(vm, key))
    value := table_get(&vm.globals, vkey)
    push_rawvalue(vm, value)
}

to_string :: proc(vm: ^VM, index: int) -> (result: string, ok: bool) {
    value := index_to_address(vm, index)
    if !value_is_string(value^) {
        return "", false
    }
    return ostring_to_string(value.ostring), true
}


/* 
**Notes**
-   Unlike the `push_*` family of functions, we reuse the `-count` stack slot
    instead of pushing.
 */
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


/* 
**Notes**
-   See the notes regarding the stack in `push_rawvalue()`.
 */
push_string :: proc(vm: ^VM, str: string) -> (result: string) {
    interned := ostring_new(vm, str)
    push_rawvalue(vm, value_make(interned))
    return ostring_to_string(interned)
}


/* 
**Notes**
-   See the notes regarding the stack in `push_rawvalue()`.
 */
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

/*
**Brief**
-   Internal use helper function.

**Assumptions**
-   Does not check if the VM stack actually has enough space to accomodate the
    pushed value.
-   You must have checked the stack beforehand and resized if necessary.

**Notes**
-   Distinct from the API function `push_value(vm: ^VM, index: int)` or
    `lua_pushvalue(lua_State *L, int i)`.
-   That function copies the element at the specified stack index and pushes it
    to the top of the stack.

**Links**
-   https://www.lua.org/pil/24.2.1.html
 */
@(private="package")
push_rawvalue :: proc(vm: ^VM, value: Value) {
    vm.top     = &vm.top[1]
    vm.top[-1] = value
}

pop :: proc(vm: ^VM, count: int) {
    vm.top = &vm.top[-count]
}
