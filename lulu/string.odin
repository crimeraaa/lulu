#+private
package lulu

import "core:fmt"
import "core:io"
import "core:mem"

OString :: struct {
    using base: Object_Header,
    hash:       u32,
    len:        int, // Length in bytes, not runes.
    data:    [0]byte,
}

ostring_formatter :: proc(info: ^fmt.Info, arg: any, verb: rune) -> bool {
    ostring := (cast(^^OString)arg.data)^
    text    := ostring_to_string(ostring)
    writer  := info.writer
    switch verb {
    case 'v', 's':
        io.write_string(writer, text, &info.n)
        return true
    case 'q':
        quote := '\'' if ostring.len == 1 else '\"'
        info.n += fmt.wprintf(writer, "%c%s%c", quote, text, quote)
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
ostring_new :: proc(vm: ^VM, input: string) -> (ostring: ^OString) {
    if prev, ok := intern_get(&vm.interned, input); ok {
        return prev
    }

    len := len(input)
    ostring      = object_new(OString, vm, len + 1)
    ostring.hash = hash_string(input)
    ostring.len  = len
    #no_bounds_check {
        copy(ostring.data[:len], input)
        ostring.data[len] = 0
    }
    intern_set(&vm.interned, ostring)
    return ostring
}

ostring_free :: proc(vm: ^VM, ostring: ^OString, location := #caller_location) {
    // We also allocated memory for the nul char for C compatibility.
    size := size_of(ostring^) + ostring.len + 1
    mem.free_with_size(ostring, size, vm.allocator, loc = location)
}

ostring_to_string :: #force_inline proc "contextless" (ostring: ^OString) -> string {
    #no_bounds_check {
        return string(ostring.data[:ostring.len])
    }
}

ostring_to_cstring :: #force_inline proc "contextless" (ostring: ^OString) -> cstring {
    #no_bounds_check {
        assert_contextless(ostring.data[ostring.len] == 0)
        return cstring(cast([^]byte)&ostring.data)
    }
}

hash_f64 :: #force_inline proc "contextless" (data: f64) -> (hash: u32) {
    data := data
    return hash_bytes(mem.byte_slice(&data, size_of(data)))
}

hash_pointer :: #force_inline proc "contextless" (data: rawptr) -> (hash: u32) {
    data := data
    return hash_bytes(mem.byte_slice(&data, size_of(data)))
}

hash_string :: #force_inline proc "contextless" (data: string) -> (hash: u32) {
    return hash_bytes(transmute([]byte)data)
}

hash_bytes :: proc "contextless" (bytes: []byte) -> (hash: u32) {
    FNV1A_OFFSET_32 :: 0x811c9dc5
    FNV1A_PRIME_32  :: 0x01000193

    hash = FNV1A_OFFSET_32
    for b in bytes {
        hash ~= cast(u32)b // Odin's bitwise XOR operator is ~
        hash *= FNV1A_PRIME_32
    }

    return hash
}
