package lulu

import "core:fmt"
import "core:c/libc"

@(private="file")
PRINT_USE_TOSTRING :: false

open_base :: proc(vm: ^VM) {
    clock :: proc(vm: ^VM, n_arg: int) -> (n_ret: int) {
        now := f64(libc.clock()) / libc.CLOCKS_PER_SEC
        push_number(vm, now)
        return 1
    }

    print :: proc(vm: ^VM, n_arg: int) -> (n_ret: int) {
        when PRINT_USE_TOSTRING {
            get_global(vm, "tostring") // ..., function: tostring
        }
        for i in 1..=n_arg {
            if i > 1 {
                fmt.print('\t')
            }
            when PRINT_USE_TOSTRING {
                push_value(vm, -1) // ..., tostring, tostring
                push_value(vm, i)  // ..., tostring, tostring, arg[i]
                call(vm, 1, 1)     // ..., tostring, tostring(arg[i])
                fmt.print(to_string(vm, -1))
                pop(vm, 1)
            } else {
                tname := type_name(vm, i)
                switch type(vm, i) {
                case .None, .Nil: fmt.print(tname)
                case .Boolean:    fmt.print(to_boolean(vm, i))
                case .Number:     fmt.print(to_number(vm, i))
                case .String:     fmt.print(to_string(vm, i))
                case .Table, .Function:
                    fmt.printf("%s: %p", tname, to_pointer(vm, i))
                }
            }
        }
        fmt.println()
        return 0
    }

    tostring :: proc(vm: ^VM, n_arg: int) -> (n_ret: int) {
        tname := type_name(vm, 1)
        switch type(vm, 1) {
        case .None:    push_string(vm, tname)
        case .Nil:     push_string(vm, tname)
        case .Boolean: push_string(vm, "true" if to_boolean(vm, 1) else "false")
        case .Number:  push_fstring(vm, NUMBER_FMT, to_number(vm, 1))
        case .String:  push_string(vm, to_string(vm, 1))
        case .Table, .Function:
            push_fstring(vm, "%s: %p", tname, to_pointer(vm, 1))
        }
        return 1
    }

    base_type :: proc(vm: ^VM, n_arg: int) -> (n_ret: int) {
        tname := type_name(vm, 1)
        push_string(vm, tname)
        return 1
    }

    @(static, rodata)
    functions := [?]Library_Function{
        {name = "print",    fn = print},
        {name = "type",     fn = base_type},
        {name = "clock",    fn = clock},
        {name = "tostring", fn = tostring},
    }

    for entry in functions {
        push_function(vm, entry.fn)
        set_global(vm, entry.name)
    }
}
