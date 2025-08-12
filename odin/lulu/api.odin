package lulu

import "core:fmt"
import "core:mem"
import "core:strings"
import "core:container/small_array"

Number :: NUMBER_TYPE

VM :: struct {
    frames:       small_array.Small_Array(MAX_FRAMES, Frame),
    current:     ^Frame,

    stack_all:  []Value, // len(stack) == stack_size
    allocator:    mem.Allocator,
    builder:      strings.Builder,
    interned:     Intern,
    globals:      Table,
    objects:     ^Object,
    view:       []Value,
    saved_ip:    ^Instruction, // Used for error reporting
    handlers:    ^Error_Handler,
}

Type :: enum {
    None = -1, // API use only; values outside the current stack view.
    Nil  =  0,
    Number,
    Boolean,
    String,
    Table,
    Function,
}

Error :: enum {
    Ok,
    Syntax,
    Runtime,
    Out_Of_Memory,
}

Library_Function :: struct {
    name: string,
    fn:   Function_Odin,
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
-   `lapi.c:lua_load(lua_State *L, lua_Reader reader, void *data,
    const char *chunkname)` and `lapi.c:lua_pcall(lua_State *L, int nargs,
    int nresults, int errfunc)` in Lua 5.1.5.
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

set_top :: proc(vm: ^VM, index: int) {
    if index >= 0 {
       base, top := vm_absindex(vm, vm.view)
       new_top := top + index
       // Zero-initialize the new region.
       for &reg in vm.stack_all[top:new_top] {
           reg = value_make()
       }
       vm.view = vm.stack_all[base:new_top]
    } else {
        // e.g. if `index == -1`, nothing changes as we still want the top to
        // be the last slot.
        pop(vm, -index + 1)
    }
}

pop :: proc(vm: ^VM, n := 1) {
    // This is generally safe, because we are *shrinking* the view.
    vm.view = vm.view[:len(vm.view) - n]
}


// You may use negative indexes to resolve from the top.
@(private="file")
poke :: proc(vm: ^VM, i: int) -> (v: ^Value, ok: bool) {
    if i > 0 {
        return poke_base(vm, i - 1)
    }
    return poke_top(vm, i)
}

@(private="file")
peek :: proc(vm: ^VM, i: int) -> Value {
    @(static, rodata)
    none := Value{type = .None}
    v, ok := poke(vm, i)
    return v^ if ok else none
}

@(private="file")
poke_base :: proc(vm: ^VM, i: int) -> (v: ^Value, ok: bool) {
    assert(i >= 0)
    if i >= len(vm.view) {
        return nil, false
    }
    return &vm.view[i], true
}

@(private="file")
poke_top :: proc(vm: ^VM, i: int) -> (v: ^Value, ok: bool) {
    assert(i <= 0)
    return poke_base(vm, len(vm.view) + i)
}

///=== PUSH FUNCTIONS ====================================================== {{{


push_nil :: proc(vm: ^VM, n := 1) {
    vm_check_stack(vm, n)
    for i in 0..<n {
        push_rawvalue(vm, value_make())
    }
}

push_boolean :: proc(vm: ^VM, b: bool) {
    push_rawvalue(vm, value_make(b))
}

push_number :: proc(vm: ^VM, n: Number) {
    push_rawvalue(vm, value_make(n))
}


/*
**Notes**
-   The returned string's memory is owned and managed by the VM.
 */
push_string :: proc(vm: ^VM, s: string) -> string {
    o := ostring_new(vm, s)
    push_rawvalue(vm, value_make(o))
    return ostring_to_string(o)
}


/*
**Notes**
-   The returned string's memory is owned and managed by the VM.
 */
push_fstring :: proc(vm: ^VM, format: string, args: ..any) -> (s: string) {
    b := vm_get_builder(vm)
    return push_string(vm, fmt.sbprintf(b, format, ..args))
}

/*
**Overview**
-   Pushes a copy of the value at index `i`.
 */
push_value :: proc(vm: ^VM, i: int) {
    push_rawvalue(vm, peek(vm, i))
}


push_function :: proc(vm: ^VM, p: Function_Odin) {
    f := function_new(vm, p)
    push_rawvalue(vm, value_make(f))
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
    // Could do something unsafe like:
    //      `vm.view = slice.from_ptr(raw_data(vm.view), len(vm.view) + 1)`
    // But then we don't have bounds checking on the full stack.
    vm_extend_view_relative(vm, &vm.view, 1)
    if ptr, ok := poke_top(vm, -1); ok {
        ptr^ = v
    }
}

///=== }}} =====================================================================

///=== }}} =====================================================================

///=== VM TYPE MANIPULATION API ============================================ {{{


type :: proc(vm: ^VM, i: int) -> Type {
    v, ok := poke(vm, i)
    return v.type if ok else .None
}

type_name :: proc {
    type_name_from_value,
    type_name_from_type,
}

type_name_from_value :: proc(vm: ^VM, i: int) -> string {
    return value_type_name(peek(vm, i))
}

type_name_from_type :: proc(vm: ^VM, t: Type) -> string {
    return value_type_names[t]
}

is_none :: proc(vm: ^VM, i: int) -> bool {
    return value_is_none(peek(vm, i))
}

is_nil :: proc(vm: ^VM, i: int) -> bool {
    return value_is_nil(peek(vm, i))
}

is_none_or_nil :: proc(vm: ^VM, i: int) -> bool {
    v := peek(vm, i)
    return value_is_none(v) || value_is_nil(v)
}

is_boolean :: proc(vm: ^VM, i: int) -> bool {
    return value_is_nil(peek(vm, i))
}

is_number :: proc(vm: ^VM, i: int) -> bool {
    return value_is_number(peek(vm, i))
}

is_string :: proc(vm: ^VM, i: int) -> bool {
    return value_is_string(peek(vm, i))
}

is_table :: proc(vm: ^VM, i: int) -> bool {
    return value_is_table(peek(vm, i))
}


/*
**Overview**
-   Gets the boolean representation of the value at stack index `i`.

**Analogous to**
-   `lapi.c:lua_toboolean(lua_State *L, int i)` in Lua 5.1.5.

**Notes** (2025-05-13)
-   If the value at relative index `i` is not a boolean, we do not convert it.
 */
to_boolean :: proc(vm: ^VM, i: int) -> bool {
    return !value_is_falsy(peek(vm, i))
}


/*
**Overview**
-   Gets the number representation of the value at stack index `i`.
-   If it is not already a number nor a string convertible to a number, `ok`
    is set to `false`.

**Analogous to**
-   `lapi.c:lua_tonumber(lua_State *L, int i)` in Lua 5.1.5.
 */
to_number :: proc(vm: ^VM, i: int) -> (n: Number, ok: bool) #optional_ok {
    v := peek(vm, i)
    if !value_is_number(v) {
        // Only strings are potentially convertible to numbers.
        value_is_string(v) or_return

        // Check if string can be parsed into a number.
        n = string_to_number(value_as_string(v)) or_return
        return n, true
    }
    return v.number, true
}


/*
**Overview**
-   Get the string representation of the value at stack index `i`.
-   If the value is a number, it is converted to a string. This also affects
    the value in the stack.
-   Other types are not implicitly converted to strings.

**Analogous to**
-   `lapi.c:lua_tolstring(lua_State *L, int index, size_t *len)` in Lua 5.1.5.
 */
to_string :: proc(vm: ^VM, i: int) -> (s: string, ok: bool) #optional_ok {
    v := poke(vm, i) or_return
    if !value_is_string(v^) {
        value_is_number(v^) or_return
        o := ostringf_new(vm, NUMBER_FMT, v.number)
        v^ = value_make(o)
    }
    return value_as_string(v^), true
}


/*
**Overview**
-   Gets the pointer representation of the value at index `i`.
-   Only 'objects', mainly values of type `table`, represented as a pointer.
 */
to_pointer :: proc(vm: ^VM, i: int) -> (p: rawptr, ok: bool) #optional_ok {
    v := peek(vm, i)
    if v.type >= .Table {
        return v.object, true
    }
    return nil, false
}


///=== }}} =====================================================================

///=== VM TABLE API ======================================================== {{{


/*
**Notes**
-   See the notes regarding the stack in `push_rawvalue()`.
 */
get_global :: proc(vm: ^VM, key: string) {
    k := value_make(ostring_new(vm, key))
    v := table_get(vm.globals, k)
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
    v := peek(vm, -1)
    table_set(vm, &vm.globals, k, v)
    pop(vm)
}

///=== }}} =====================================================================


///=== VM MISCALLENOUS API ================================================= {{{


// Unconditionally throws; makes no assumptions if any values were pushed.
error :: proc(vm: ^VM) -> ! {
    vm_throw(vm, .Runtime)
}

// Unconditionally throws and leaves an error message at the top of the stack.
errorf :: proc(vm: ^VM, format: string, args: ..any) -> ! {
    vm_runtime_error(vm, format, ..args)
}

call :: proc(vm: ^VM, n_arg, n_ret: int) {
    assert(n_arg != VARARG && n_arg >= 0)
    assert(n_ret != VARARG && n_ret >= 0)

    // -narg is the first argument, so -(narg + 1) is the function itself.
    callable, ok := poke_top(vm, -(n_arg + 1))
    fn_index := ptr_index(callable, vm.stack_all)

    // Account for any changes in the stack pushed by native functions.
    if vm.current != nil {
        assert(raw_data(vm.view) == raw_data(vm.current.window))
        top := vm_absindex(vm, vm.view, from = .Top)
        vm_extend_view_absolute(vm, &vm.current.window, top)
    }
    if vm_call(vm, callable, n_arg, n_ret) == .Lua {
        vm_execute(vm)
    }

    /*
    **Notes** (2025-06-10):
    -   Points exactly up to the last return value.
    -   Concept check `function f() return "hi" end; print(f())`
     */
    vm_extend_view_absolute(vm, &vm.view, fn_index + n_ret)
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
    target: [^]Value
    target, _ = poke_top(vm, -count)
    vm_concat(vm, target, target[:count])
    pop(vm, count - 1)
}

///=== }}} =====================================================================

