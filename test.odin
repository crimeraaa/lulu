package lulu_main

import "core:testing"

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
    testing.expect(t, lulu.run(vm, input, loc.procedure) == .None)
}

@test
hello :: proc(t: ^testing.T) {
    vm := new_vm(t)
    run_string(t, vm, `print("Hi mom!")`)
}
