package lulu

import "core:fmt"
import "core:mem"

State :: VM

Status :: enum {
    Ok,
    Compile_Error,
    Runtime_Error,
    Out_Of_Memory,
}

///=== VM SETUP/TEARDOWN =================================================== {{{


@(require_results)
open :: proc(allocator := context.allocator) -> (vm: ^State, err: mem.Allocator_Error) {
    vm = new(State, allocator) or_return
    if vm_init(vm, allocator) {
        return vm, nil
    }
    return nil, .Out_Of_Memory
}

close :: proc(vm: ^State) {
    vm_destroy(vm)
    free(vm, vm.allocator)
}


/*
**Analogous to**
-   `lapi.c:lua_load(lua_State *L, lua_Reader reader, void *data, const char *chunkname)`
-   `lapi.c:lua_pcall(lua_State *L, int nargs, int nresults, int errfunc)`
 */
run :: proc(vm: ^State, input, source: string) -> Status {
    return vm_interpret(vm, input, source)
}


///=== }}} =====================================================================

///=== VM STACK MANIPULATION =============================================== {{{


/*
**Overview**
-   Get the index of the current stack top.
-   This also represents the size of the current stack frame.

**Analogous to**
-   `int lua_gettop(lua_State *L)`.
 */
get_top :: proc(vm: ^State) -> (index: int) {
    return ptr_sub(vm.top, vm.base)
}


pop :: proc(vm: ^State, count: int) {
    vm.top = &vm.top[-count]
}


// You may use negative indexes to resolve from the top.
@(private="file")
index_to_address :: proc(vm: ^State, index: int) -> ^Value {
    // If negative we will index relative to the top
    from := vm.base if index >= 0 else vm.top
    return &from[index]
}


/*
**Notes**
-   See the notes regarding the stack in `push_rawvalue()`.
 */
push_string :: proc(vm: ^State, str: string) -> (result: string) {
    interned := ostring_new(vm, str)
    push_rawvalue(vm, value_make(interned))
    return ostring_to_string(interned)
}


/*
**Notes**
-   See the notes regarding the stack in `push_rawvalue()`.
 */
push_fstring :: proc(vm: ^State, format: string, args: ..any) -> (result: string) {
    builder := vm_get_builder(vm)
    return push_string(vm, fmt.sbprintf(builder, format, ..args))
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
push_rawvalue :: proc(vm: ^State, value: Value) {
    vm.top     = &vm.top[1]
    vm.top[-1] = value
}


///=== }}} =====================================================================

///=== VM TYPE CONVERSION API ============================================== {{{


to_string :: proc(vm: ^State, index: int) -> (result: string, ok: bool) {
    value := index_to_address(vm, index)
    if !value_is_string(value^) {
        return "", false
    }
    return value_to_string(value^), true
}


///=== }}} =====================================================================

///=== VM TABLE API ======================================================== {{{


/*
**Notes**
-   See the notes regarding the stack in `push_rawvalue()`.
 */
get_global :: proc(vm: ^State, key: string) {
    vkey  := value_make(ostring_new(vm, key))
    value := table_get(&vm.globals, vkey)
    push_rawvalue(vm, value)
}


/*
**Notes**
-   Assumes the value we want to set with is at the very top of the stack,
    a.k.a. at index `-1`.
-   We will pop this value afterwards.
 */
set_global :: proc(vm: ^State, key: string) {
    vkey := value_make(ostring_new(vm, key))
    table_set(vm, &vm.globals, vkey, vm.top[-1])
    pop(vm, 1)
}

///=== }}} =====================================================================


///=== VM MISCALLENOUS API ================================================= {{{

/*
**Notes**
-   Unlike the `push_*` family of functions, we reuse the `-count` stack slot
    instead of pushing.
 */
concat :: proc(vm: ^State, count: int) {
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

///=== }}} =====================================================================

