package main

import "core:testing"
import "core:fmt"

import "lulu"

@(private="file")
new_vm :: proc(t: ^testing.T) -> ^lulu.VM {
    teardown :: proc(user_ptr: rawptr) {
        lulu.close(cast(^lulu.VM)user_ptr)
    }

    vm, err := lulu.open()
    if err != nil {
        testing.fail_now(t, "out of memory")
    }
    testing.cleanup(t, teardown, vm)
    return vm
}

@(private="file")
run_string :: proc(t: ^testing.T, vm: ^lulu.VM, input: string, loc := #caller_location) {
    testing.expect(t, lulu.run(vm, input, loc.procedure) == .Ok)
}

@test
hello :: proc(t: ^testing.T) {
    vm := new_vm(t)
    run_string(t, vm, `print("Hi mom!")`)
}

@test
arith :: proc(t: ^testing.T) {
    vm := new_vm(t)
    run_arith(t, vm, 1.0 + 2.0*3.0 - 4.0/-5.0)
    run_arith(t, vm, (1.0 + 2.0)*3.0 - 4.0/-5.0)
}

@(private="file")
run_arith :: proc(t: ^testing.T, vm: ^lulu.VM, $n: f64, expr := #caller_expression(n), loc := #caller_location) {
    line := fmt.tprintf("return %s", expr)
    run_string(t, vm, line, loc = loc)
    testing.expect(t, lulu.to_number(vm, -1) == n)
}
