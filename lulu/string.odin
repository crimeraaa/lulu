#+private
package lulu

import "core:fmt"
import "core:io"
import "core:mem"

OString :: struct {
    using base: Object,
    hash:       u32,
    len:        int, // Length in bytes, not runes.
    data:    [0]byte,
}

ostring_formatter :: proc(fi: ^fmt.Info, arg: any, verb: rune) -> bool {
    s := ostring_to_string((cast(^^OString)arg.data)^)
    switch verb {
    case 'v', 's':
        io.write_string(fi.writer, s, &fi.n)
    case 'q':
        io.write_quoted_string(fi.writer, s, '\'' if len(s) == 1 else '\"', &fi.n)
    case:
        return false
    }
    return true
}


/*
**Notes** (2025-01-13)
-   These strings are compatible with C-style strings.
    However, extracting the cstring requires an unsafe cast.
 */
ostring_new :: proc(vm: ^VM, input: string) -> ^OString {
    if prev, ok := intern_get(&vm.interned, input); ok {
        return prev
    }

    n := len(input)
    s := object_new(OString, vm, n + 1)

    s.hash = hash_string(input)
    s.len  = n
    #no_bounds_check {
        copy(s.data[:n], input)
        s.data[n] = 0
    }
    intern_set(&vm.interned, s)
    return s
}

ostringf_new :: proc(vm: ^VM, format: string, args: ..any) -> ^OString {
    b := vm_get_builder(vm)
    s := fmt.sbprintf(b, format, ..args)
    return ostring_new(vm, s)
}

ostring_free :: proc(vm: ^VM, s: ^OString, location := #caller_location) {
    // We also allocated memory for the nul char for C compatibility.
    size := size_of(s^) + s.len + 1
    mem.free_with_size(s, size, vm.allocator, loc = location)
}

ostring_to_string :: #force_inline proc "contextless" (s: ^OString) -> string {
    #no_bounds_check {
        return string(s.data[:s.len])
    }
}

ostring_to_cstring :: #force_inline proc "contextless" (s: ^OString) -> cstring {
    #no_bounds_check {
        assert_contextless(s.data[s.len] == 0)
        return cstring(cast([^]byte)&s.data)
    }
}

hash_number :: #force_inline proc "contextless" (n: Number) -> (hash: u32) {
    n := n
    return hash_bytes(mem.byte_slice(&n, size_of(n)))
}

hash_pointer :: #force_inline proc "contextless" (p: rawptr) -> (hash: u32) {
    p := p
    return hash_bytes(mem.byte_slice(&p, size_of(p)))
}

hash_string :: #force_inline proc "contextless" (s: string) -> (hash: u32) {
    /*
    **Notes** (2025-05-17)
    -   This is safe, because `hash_bytes()` does not mutate `bytes` at all.
     */
    return hash_bytes(transmute([]byte)s)
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
