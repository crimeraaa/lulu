#+private
package lulu

import "core:fmt"
import "core:io"


Value :: struct {
    type:       Type,
    using data: Value_Data,
}

Value_Data :: struct #raw_union {
    number:   Number,
    boolean:  bool,
    ostring: ^OString,
    table:   ^Table,
}

// Used for callbacks/dispatches
Number_Arith_Proc   :: #type proc "contextless" (a, b: Number) -> Number
Number_Compare_Proc :: #type proc "contextless" (a, b: Number) -> bool

value_make :: proc {
    value_make_nil,
    value_make_boolean,
    value_make_number,
    value_make_integer,
    value_make_string,
    value_make_table,
}

value_type_name :: #force_inline proc "contextless" (v: Value) -> string {
    @(static, rodata)
    type_names := [Type]string {
        .None    = "no value",
        .Nil     = "nil",
        .Boolean = "boolean",
        .Number  = "number",
        .String  = "string",
        .Table   = "table",
    }
    return type_names[v.type]
}

value_make_nil :: #force_inline proc "contextless" () -> Value {
    return Value{type = .Nil}
}

value_make_boolean :: #force_inline proc "contextless" (b: bool) -> Value {
    return Value{type = .Boolean, boolean = b}
}

value_make_number :: #force_inline proc "contextless" (n: Number) -> Value {
    return Value{type = .Number, number = n}
}

value_make_integer :: #force_inline proc "contextless" (i: int) -> Value {
    return value_make_number(cast(Number)i)
}

value_make_string :: #force_inline proc "contextless" (s: ^OString) -> Value {
    return Value{type = .String, ostring = s}
}

value_make_table :: #force_inline proc "contextless" (t: ^Table) -> Value {
    return Value{type = .Table, table = t}
}

value_is_nil :: #force_inline proc "contextless" (v: Value) -> bool {
    return v.type == .Nil
}

value_is_boolean :: #force_inline proc "contextless" (v: Value) -> bool {
    return v.type == .Boolean
}

value_is_falsy :: #force_inline proc "contextless" (v: Value) -> bool {
    return v.type == .Nil || (v.type == .Boolean && !v.boolean)
}

value_is_number :: #force_inline proc "contextless" (v: Value) -> bool {
    return v.type == .Number
}

value_is_string :: #force_inline proc "contextless" (v: Value) -> bool {
    return v.type == .String
}

value_is_table :: #force_inline proc "contextless" (v: Value) -> bool {
    return v.type == .Table
}

// Utility function because this is so common.
value_as_string :: #force_inline proc "contextless" (v: Value) -> string {
    return ostring_to_string(v.ostring)
}

value_eq :: proc(a, b: Value) -> bool {
    if a.type != b.type {
        return false
    }

    switch a.type {
    case .None:     break
    case .Nil:      return true
    case .Boolean:  return a.boolean == b.boolean
    case .Number:   return number_eq(a.number, b.number)
    case .String:   return a.ostring == b.ostring
    case .Table:    return a.table == b.table
    }
    unreachable("Unknown value type %v", a.type)
}

value_formatter :: proc(fi: ^fmt.Info, arg: any, verb: rune) -> bool {
    basic_print :: proc(fi: ^fmt.Info, v: Value) {
        switch v.type {
        case .None:     io.write_string(fi.writer, "none", &fi.n)
        case .Nil:      io.write_string(fi.writer, "nil", &fi.n)
        case .Boolean:  io.write_string(fi.writer, "true" if v.boolean else "false", &fi.n)
        case .Number:   fi.n += fmt.wprintf(fi.writer, NUMBER_FMT, v.number)
        case .String:   io.write_string(fi.writer, value_as_string(v), &fi.n)
        case .Table:
            type_name := value_type_name(v)
            pointer   := cast(rawptr)v.table
            fi.n += fmt.wprintf(fi.writer, "%s: %p", type_name, pointer)
        case:
            unreachable("Unknown value type %v", v.type)
        }
    }

    modified_print :: proc(fi: ^fmt.Info, v: Value, is_stack := false) {
        if is_stack {
            io.write_string(fi.writer, "\t[ ", &fi.n)
        }
        defer if is_stack {
            io.write_string(fi.writer, " ]", &fi.n)
        }

        if value_is_string(v) {
            s := value_as_string(v)
            io.write_quoted_string(fi.writer, s, '\'' if len(s) == 1 else '\"', &fi.n)
        } else {
            basic_print(fi, v)
        }
    }

    v := (cast(^Value)arg.data)^
    switch verb {
    case 'v': basic_print(fi, v)
    case 'd': modified_print(fi, v)
    case 's': modified_print(fi, v, is_stack = true)
    case:     return false
    }
    return true
}
