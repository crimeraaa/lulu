#+private
package lulu

import "core:fmt"
import "core:math"

Value  :: f64
Number :: f64

Value_Print_Mode :: enum u8 {
    Normal, // Prints the value as-is and prints a newline.
    Debug,  // Same as .Normal, but surrounds strings with quotes.
    Stack,  // Prints the value surrounded by square brackets. No newline.
}

// Used for callbacks/dispatches
Number_Proc :: #type proc(a, b: Number) -> Number

number_add :: proc(a, b: Number) -> Number {
    return a + b
}

number_sub :: proc (a, b: Number) -> Number {
    return a - b
}

number_mul :: proc(a, b: Number) -> Number {
    return a * b
}

number_div :: proc(a, b: Number) -> Number {
    return a / b
}

number_mod :: proc(a, b: Number) -> Number {
    return math.mod(a, b)
}

number_pow :: proc(a, b: Number) -> Number {
    return math.pow(a, b)
}

number_unm :: proc(a: Number) -> Number {
    return -a
}

value_eq :: proc(a, b: Value) -> bool {
    return a == b
}

number_is_nan :: math.is_nan

value_print :: proc(value: Value, mode := Value_Print_Mode.Normal) {
    // We assume this is valid for numbers, booleans and pointers.
    buf: [64]byte
    s := value_to_string(buf[:], value)
    switch mode {
    case .Normal:   fallthrough
    case .Debug:    fmt.println(s)
    case .Stack:    fmt.printf("[ %s ]", s)
    }
}

value_to_string :: proc(buf: []byte, value: Value) -> string {
    fmt.bprintf(buf, "%.14g", value)
    return string(buf[:])
}
