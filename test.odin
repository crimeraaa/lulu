package main

import "core:testing"
import "core:fmt"
import "core:strings"

import "lulu"

// To differentiate from no value expected
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

run_string :: proc(t: ^testing.T, vm: ^lulu.VM, input: string, need_return := true, expected: Value = nil, loc := #caller_location) {
    line: string
    if need_return && !strings.contains(input, "return ") {
        line = fmt.tprintf("return %s", input)
    } else {
        line = input
    }
    testing.expect_value(t, lulu.run(vm, line, loc.procedure), lulu.Error.Ok,
        loc = loc, value_expr = line)

    if !need_return {
        return
    }

    switch v in expected {
    case nil:         testing.expect(t, lulu.is_nil(vm, -1), loc = loc)
    case bool:        testing.expect_value(t, lulu.to_boolean(vm, -1), v, loc = loc)
    case lulu.Number: testing.expect_value(t, lulu.to_number(vm,  -1), v, loc = loc)
    case string:      testing.expect_value(t, lulu.to_string(vm,  -1), v, loc = loc)
    }
}

@test
hello :: proc(t: ^testing.T) {
    vm := open_safe(t)
    run_string(t, vm, `print("Hi mom!")`, need_return = false)
}

@test
arith :: proc(t: ^testing.T) {
    vm := open_safe(t)
    res1 :: 1 + 2*3 - 4.0/-5
    res2 :: (1 + 2)*3 - 4.0/-5
    run_string(t, vm, `1 + 2*3 - 4/-5`,   expected = res1)
    run_string(t, vm, `(1 + 2)*3 - 4/-5`, expected = res2)

    run_string(t, vm, `x,y,z,a,b = 1,2,3,4,5`, need_return = false)
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

@test
locals :: proc(t: ^testing.T) {
    vm := open_safe(t)

    run_string(t, vm,
`local x = 13
do
    local x = x + 2 -- 13 + 2 = 15
    do
        local x = x * 2 -- 15 * 2 = 30
        INNER = x
    end
    MIDDLE = x
end
OUTER = x
return OUTER, MIDDLE, INNER
`, need_return = false)
    testing.expect_value(t, lulu.to_number(vm, 1), 13)
    testing.expect_value(t, lulu.to_number(vm, 2), 15)
    testing.expect_value(t, lulu.to_number(vm, 3), 30)
}

@test
logic_literals :: proc(t: ^testing.T) {
    vm := open_safe(t)

    // 2 operand `and`
    run_string(t, vm, `true and false`, expected = false)
    run_string(t, vm, `true and nil`,   expected = nil)
    run_string(t, vm, `false and true`, expected = false) // falsy short circuit
    run_string(t, vm, `false and nil`,  expected = false) // ^ditto
    run_string(t, vm, `nil and false`,  expected = nil)   // ^ditto

    // 2 operand `or`
    run_string(t, vm, `true or false`,  expected = true)
    run_string(t, vm, `false or true`,  expected = true)
    run_string(t, vm, `false or nil`,   expected = nil)
    run_string(t, vm, `nil or false`,   expected = false)

    // 3 operand `and` and `or`
    run_string(t, vm, `true and false or nil`, expected = nil)
    run_string(t, vm, `true and nil or false`, expected = false)
    run_string(t, vm, `false and true or nil`, expected = nil)
    run_string(t, vm, `false and nil or true`, expected = true)
    run_string(t, vm, `nil and true or false`, expected = false)
    run_string(t, vm, `nil and false or true`, expected = true)
}

@test
logic_variables :: proc(t: ^testing.T) {
    vm := open_safe(t)

    run_string(t, vm, `local x, y; return x and y`, expected = nil)
    run_string(t, vm, `local x, y = 1, 2; return x and y`, expected = 2)
    run_string(t, vm, `local x, y = 9, false; return x and y`, expected = false)
    run_string(t, vm, `local x, y = 10, nil; return x and y`, expected = nil)
    run_string(t, vm, `local x, y = false, 11; return x and y`, expected = false)
    run_string(t, vm, `local x, y = nil, 12; return x and y`, expected = nil)

    // Ternary-like operator
    run_string(t, vm, `local x, y; return x and y or "neither"`, expected = "neither")
    run_string(t, vm, `local x, y = "hi", "mom"; return x and y or "neither"`, expected = "mom")
}
