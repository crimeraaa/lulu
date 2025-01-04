package lulu

import "core:fmt"

Value :: f64

Value_Print_Mode :: enum u8 {
    Normal, // Prints the value as-is and prints a newline.
    Debug,  // Same as .Normal, but surrounds strings with quotes.
    Stack,  // Prints the value surrounded by square brackets. No newline.
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
