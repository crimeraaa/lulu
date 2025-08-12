package lulu

import "core:fmt"
import "core:c/libc"
import "core:time"

open_base :: proc(vm: ^VM) {
    clock :: proc(vm: ^VM, _: int) -> (n_ret: int) {
        now := f64(libc.clock()) / libc.CLOCKS_PER_SEC
        push_number(vm, now)
        return 1
    }

    date :: proc(vm: ^VM, _: int) -> (n_ret: int) {
        year, month, day := time.date(time.now())
        push_number(vm, Number(year))
        push_fstring(vm, "%s", month)
        push_number(vm, Number(day))
        return 3
    }

    print :: proc(vm: ^VM, n_arg: int) -> (n_ret: int) {
        get_global(vm, "tostring") // ..., tostring
        for i in 1..=n_arg {
            if i > 1 {
                fmt.print('\t')
            }
            push_value(vm, -1)
            push_value(vm, i)  // ..., tostring, tostring, arg[i]
            call(vm, 1, 1)     // ..., tostring, tostring(arg[i])
            fmt.print(to_string(vm, -1))
            pop(vm, 1)
        }
        fmt.println()
        return 0
    }

    tostring :: proc(vm: ^VM, n_arg: int) -> (n_ret: int) {
        // TODO(2025-06-09): Mimic `lauxlib.c:luaL_argerror()`
        // Need to implement `lua_Debug` analog first though!
        if n_arg == 0 {
            errorf(vm, "tostring: 1 argument expected")
        }

        tname := type_name(vm, 1)
        switch type(vm, 1) {
        case .None, .Nil: push_string(vm, tname)
        case .Boolean:    push_string(vm, "true" if to_boolean(vm, 1) else "false")
        case .Number:     push_fstring(vm, NUMBER_FMT, to_number(vm, 1))
        case .String:     break // Already a string, nothing to do
        case .Table, .Function:
            push_fstring(vm, "%s: %p", tname, to_pointer(vm, 1))
        }
        return 1
    }

    base_type :: proc(vm: ^VM, n_arg: int) -> (n_ret: int) {
        if n_arg == 0 {
            errorf(vm, "type: 1 argument expected")
        }
        tname := type_name(vm, 1)
        push_string(vm, tname)
        return 1
    }

    @(static, rodata)
    functions := [?]Library_Function{
        {name = "clock",    fn = clock},
        {name = "date",     fn = date},
        {name = "print",    fn = print},
        {name = "tostring", fn = tostring},
        {name = "type",     fn = base_type},
    }

    push_rawvalue(vm, value_make(&vm.globals))
    set_global(vm, "_G")

    for entry in functions {
        push_function(vm, entry.fn)
        set_global(vm, entry.name)
    }
}
