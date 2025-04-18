#+private
package lulu

import "core:fmt"
import "core:io"
import "core:mem"
import "core:unicode/utf8"

OString :: struct {
    using base: Object_Header,
    hash:       u32,
    len:        int, // Length in bytes, not runes.
    data:    [0]byte,
}

ostring_formatter :: proc(fi: ^fmt.Info, arg: any, verb: rune) -> bool {
    ostring   := (cast(^^OString)arg.data)^
    s         := ostring_to_string(ostring)
    n_written := &fi.n
    writer    := fi.writer
    switch verb {
    case 'v', 's':
        io.write_string(writer, s, n_written)
        return true
    case 'q':
        n_written^ += fmt.wprintf(writer, "%q", s)
        return true
    case:
        return false
    }
}

/*
Notes:
-   These strings are compatible with C-style strings.
    However, extracting the cstring requires an unsafe cast.
 */
ostring_new :: proc(vm: ^VM, input: string) -> (str: ^OString) {
    if prev, ok := intern_get(&vm.interned, input); ok {
        return prev
    }

    n  := len(input)
    str = object_new(OString, vm, n + 1)
    defer intern_set(&vm.interned, str)

    str.hash = hash_string(input)
    str.len  = n
    #no_bounds_check {
        copy(str.data[:n], input)
        str.data[n] = 0
    }
    return str
}

ostring_free :: proc(vm: ^VM, str: ^OString, location := #caller_location) {
    // We also allocated memory for the nul char for C compatibility.
    mem.free_with_size(str, size_of(str^) + str.len + 1, vm.allocator, loc = location)
}

ostring_to_string :: proc(str: ^OString) -> string #no_bounds_check {
    return string(str.data[:str.len])
}

ostring_to_cstring :: proc(str: ^OString) -> cstring #no_bounds_check {
    assert(str.data[str.len] == 0)
    return cstring(cast([^]byte)&str.data)
}

hash_f64 :: proc(data: f64) -> (hash: u32) {
    data := data
    return hash_bytes(mem.byte_slice(&data, size_of(data)))
}

hash_pointer :: proc(data: rawptr) -> (hash: u32) {
    data := data
    return hash_bytes(mem.byte_slice(&data, size_of(data)))
}

hash_string :: proc(data: string) -> (hash: u32) {
    return hash_bytes(transmute([]byte)data)
}

hash_bytes :: proc(bytes: []byte) -> (hash: u32) {
    FNV1A_OFFSET_32 :: 0x811c9dc5
    FNV1A_PRIME_32  :: 0x01000193

    hash = FNV1A_OFFSET_32
    for b in bytes {
        hash ~= cast(u32)b // Odin's bitwise XOR operator is ~
        hash *= FNV1A_PRIME_32
    }

    return hash
}
