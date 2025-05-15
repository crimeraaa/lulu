package lulu

import "core:fmt"
import "core:mem"
import "core:strings"

VM :: struct {
    stack_all:  []Value, // len(stack) == stack_size
    allocator:    mem.Allocator,
    builder:      strings.Builder,
    interned:     Intern,
    globals:      Table,
    objects:     ^Object,
    view:       []Value,
    chunk:       ^Chunk,
    saved_ip:    ^Instruction, // Used for error reporting
    handlers:    ^Error_Handler,
}

Type :: enum {
    Nil,
    Number,
    Boolean,
    String,
    Table,
}

Error :: enum {
    None,
    Compile_Error,
    Runtime_Error,
    Out_Of_Memory,
}

///=== VM SETUP/TEARDOWN =================================================== {{{


@(require_results)
open :: proc(allocator := context.allocator) -> (vm: ^VM, err: mem.Allocator_Error) {
    vm, err = new(VM, allocator)
    if err == nil && vm_init(vm, allocator) {
        return vm, nil
    }
    free(vm, allocator)
    return nil, .Out_Of_Memory
}

close :: proc(vm: ^VM) {
    vm_destroy(vm)
    mem.free(vm, vm.allocator)
}


/*
**Analogous to**
-   `lapi.c:lua_load(lua_State *L, lua_Reader reader, void *data, const char *chunkname)`
-   `lapi.c:lua_pcall(lua_State *L, int nargs, int nresults, int errfunc)`
 */
run :: proc(vm: ^VM, input, source: string) -> Error {
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
get_top :: proc(vm: ^VM) -> (index: int) {
    return len(vm.view)
}


pop :: proc(vm: ^VM, count: int) {
    vm.view = vm.view[:len(vm.view) - 1]
}


// You may use negative indexes to resolve from the top.
@(private="file")
poke :: proc(vm: ^VM, i: int) -> ^Value {
    return poke_base(vm, i - 1) if i > 0 else poke_top(vm, i)
}

@(private="file")
poke_base :: proc(vm: ^VM, i: int) -> ^Value {
    assert(i >= 0)
    return &vm.view[i]
}

@(private="file")
poke_top :: proc(vm: ^VM, i: int) -> ^Value {
    assert(i <= 0)
    return &vm.view[len(vm.view) + i]
}


/*
**Notes**
-   See the notes regarding the stack in `push_rawvalue()`.
-   The returned string's memory is owned and managed by the VM.
 */
push_string :: proc(vm: ^VM, s: string) -> string {
    o := ostring_new(vm, s)
    push_rawvalue(vm, value_make(o))
    return ostring_to_string(o)
}


/*
**Notes**
-   See the notes regarding the stack in `push_rawvalue()`.
-   The returned string's memory is owned and managed by the VM.
 */
push_fstring :: proc(vm: ^VM, format: string, args: ..any) -> (s: string) {
    b := vm_get_builder(vm)
    return push_string(vm, fmt.sbprintf(b, format, ..args))
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
push_rawvalue :: proc(vm: ^VM, v: Value) {
    base := vm_view_index_in_stack(vm, .Base)
    top  := vm_view_index_in_stack(vm, .Top)
    vm.view = vm.stack_all[base:top + 1]
    poke_top(vm, -1)^ = v
}


///=== }}} =====================================================================

///=== VM TYPE CONVERSION API ============================================== {{{


type :: proc(vm: ^VM, i: int) -> Type {
    return poke(vm, i).type
}

type_name :: proc(vm: ^VM, i: int) -> string {
    return value_type_name(poke(vm, i)^)
}

/*
**Notes** (2025-05-13)
-   If the value at relative index `i` is not yet a boolean, we will convert it
    to one first.
 */
to_boolean :: proc(vm: ^VM, i: int) -> bool {
    v := poke(vm, i)
    if !value_is_boolean(v^) {
        v^ = value_make(!value_is_falsy(v^))
    }
    return v.boolean
}


/*
**Analogous to**
-   `lapi.c:lua_tolstring(lua_State *L, int index, size_t *len)` in Lua 5.1.5.

**Notes** (2025-05-14):
-   Unlike Lua, this API function allows values at index `i` to be of any type.
-   If we add metamethods (specifically `__tostring`), we should decide if
    we want this function to call that or to just use the default behavior.
-   The idea is that the global `tostring()` function should handle that!
 */
to_string :: proc(vm: ^VM, i: int) -> (s: string, ok: bool) #optional_ok {
    v := poke(vm, i)
    o: ^OString
    switch v.type {
    case .Nil:     o = ostring_new(vm, "nil")
    case .Boolean: o = ostring_new(vm, "true" if v.boolean else "false")
    case .Number:  o = ostringf_new(vm, NUMBER_FMT, v.number)
    case .String:  break
    case .Table:   o = ostringf_new(vm, "%s: %p", value_type_name(v^), v.table)
    case:
        unreachable("Cannot convert value type %v to a string", v.type)
    }
    if o != nil {
        v^ = value_make(o)
    }
    return value_as_string(v^), true
}

to_number :: proc(vm: ^VM, i: int) -> (n: f64, ok: bool) #optional_ok {
    v := poke(vm, i)
    if !value_is_number(v^) {
        value_is_string(v^) or_return
        n  = string_to_number(value_as_string(v^)) or_return
        v^ = value_make(n)
    }
    return v.number, true
}


///=== }}} =====================================================================

///=== VM TABLE API ======================================================== {{{


/*
**Notes**
-   See the notes regarding the stack in `push_rawvalue()`.
 */
get_global :: proc(vm: ^VM, key: string) {
    k := value_make(ostring_new(vm, key))
    v := table_get(&vm.globals, k)
    push_rawvalue(vm, v)
}


/*
**Notes**
-   Assumes the value we want to set with is at the very top of the stack,
    a.k.a. at index `-1`.
-   We will pop this value afterwards.
 */
set_global :: proc(vm: ^VM, key: string) {
    k := value_make(ostring_new(vm, key))
    v := poke_top(vm, -1)^
    table_set(vm, &vm.globals, k, v)
    pop(vm, 1)
}

///=== }}} =====================================================================


///=== VM MISCALLENOUS API ================================================= {{{


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
    target: [^]Value = poke_top(vm, -count)
    vm_concat(vm, target, target[:count])
    pop(vm, count - 1)
}

///=== }}} =====================================================================

