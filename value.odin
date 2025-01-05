package lulu

import "core:fmt"
import "core:math"

Value :: f64

Value_Print_Mode :: enum u8 {
    Normal, // Prints the value as-is and prints a newline.
    Debug,  // Same as .Normal, but surrounds strings with quotes.
    Stack,  // Prints the value surrounded by square brackets. No newline.
}

value_add :: proc(a, b: Value) -> Value {
    return a + b
}

value_sub :: proc (a, b: Value) -> Value {
    return a - b
}

value_mul :: proc(a, b: Value) -> Value {
    return a * b
}

value_div :: proc(a, b: Value) -> Value {
    return a / b
}

value_mod :: proc(a, b: Value) -> Value {
    return math.mod(a, b)
}

value_pow :: proc(a, b: Value) -> Value {
    return math.pow(a, b)
}

value_eq :: proc(a, b: Value) -> bool {
    return a == b
}

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
