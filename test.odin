package main

import "core:testing"
import "core:fmt"
import "core:strings"

import "lulu"

Value :: union {bool, lulu.Number, string}

@(deferred_out=lulu.close)
open_safe :: proc(t: ^testing.T) -> ^lulu.VM {
    vm, err := lulu.open()
    if err != nil {
        testing.fail_now(t, "out of memory")
    }
    lulu.open_base(vm)
    return vm
}

run_string :: proc(t: ^testing.T, vm: ^lulu.VM, input: string, expected: Value = nil, loc := #caller_location) {
    line: string
    if expected != nil && !strings.contains(input, "return ") {
        line = fmt.tprintf("return %s", input)
    } else {
        line = input
    }
    testing.expect_value(t, lulu.run(vm, line, loc.procedure), lulu.Error.Ok, loc = loc)
    switch v in expected {
    case bool:        testing.expect_value(t, lulu.to_boolean(vm, -1), v, loc = loc)
    case lulu.Number: testing.expect_value(t, lulu.to_number(vm,  -1), v, loc = loc)
    case string:      testing.expect_value(t, lulu.to_string(vm,  -1), v, loc = loc)
    }
}

@test
hello :: proc(t: ^testing.T) {
    vm := open_safe(t)
    run_string(t, vm, `print("Hi mom!")`)
}

@test
arith :: proc(t: ^testing.T) {
    vm := open_safe(t)
    res1 :: 1 + 2*3 - 4.0/-5
    res2 :: (1 + 2)*3 - 4.0/-5
    run_string(t, vm, `1 + 2*3 - 4/-5`,   expected = res1)
    run_string(t, vm, `(1 + 2)*3 - 4/-5`, expected = res2)

    run_string(t, vm, `x,y,z,a,b = 1,2,3,4,5`)
    run_string(t, vm, `x + y*z - a/-b`, expected = res1)
    run_string(t, vm, `(x+y)*z - a/-b`, expected = res2)
}

@test
compare :: proc(t: ^testing.T) {
    vm := open_safe(t)
    run_string(t, vm, `2 < 3`,  expected = true)
    run_string(t, vm, `3 <= 2`, expected = false)
    run_string(t, vm, `3 > 2`,  expected = true)
    run_string(t, vm, `2 >= 3`, expected = false)
    run_string(t, vm, `2 == 2`, expected = true)
}

@test
globals :: proc(t: ^testing.T) {
    vm := open_safe(t)
    run_string(t, vm, `PI, G = 3.14, -9.81; return PI == 3.14`, expected = true)
    run_string(t, vm, `G == -9.81`, expected = true)
}
