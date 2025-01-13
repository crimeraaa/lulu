package lulu

import "base:intrinsics"
import "core:fmt"
import "core:mem"
import "core:strings"

OString :: struct {
    using base  : Object_Header,
    hash        : u32,
    len         : int,
    data        : [0]byte,
}

/*
Notes:
-   These strings are compatible with C-style strings.
    However, extracting the cstring requires an unsafe cast.
 */
ostring_new :: proc(vm: ^VM, input: string) -> (str: ^OString) {
    fmt.printf("Processing string %q (len: %i)... ", input, len(input))
    defer fmt.println(vm.interned)
    if prev, ok := vm.interned[input]; ok {
        fmt.println("Interned!")
        return prev
    }
    fmt.println("Interning...")
    n  := len(input)
    str = object_new(OString, vm, n + 1)
    defer vm.interned[input] = str

    str.hash = fnv1a_hash_32(input)
    str.len  = n
    #no_bounds_check {
        copy(str.data[:n], input)
        str.data[n] = 0
    }
    return str
}

ostring_free :: proc(vm: ^VM, str: ^OString) {
    object_unlink(vm, &str.base)
    mem.free_with_size(str, size_of(str^) + str.len + 1, vm.allocator)
}

ostring_to_string :: proc(str: ^OString) -> string #no_bounds_check {
    return string(str.data[:str.len])
}

ostring_to_cstring :: proc(str: ^OString) -> cstring #no_bounds_check {
    assert(str.data[str.len] == 0)
    return cstring(cast([^]byte)&str.data)
}

fnv1a_hash_32 :: proc {
    fnv1a_hash_32_bytes,
    fnv1a_hash_32_string,
    fnv1a_hash_32_f64,
    fnv1a_hash_32_pointer,
}

fnv1a_hash_32_f64 :: proc(data: f64) -> (hash: u32) {
    data := data
    return fnv1a_hash_32_bytes(mem.byte_slice(&data, size_of(data)))
}

fnv1a_hash_32_pointer :: proc(data: rawptr) -> (hash: u32) {
    data := data
    return fnv1a_hash_32_bytes(mem.byte_slice(&data, size_of(data)))
}

fnv1a_hash_32_string :: proc(data: string) -> (hash: u32) {
    return fnv1a_hash_32_bytes(transmute([]byte)data)
}

fnv1a_hash_32_bytes :: proc(bytes: []byte) -> (hash: u32) {
    FNV1A_OFFSET_32 :: 0x811c9dc5
    FNV1A_PRIME_32  :: 0x01000193

    hash = FNV1A_OFFSET_32
    for b in bytes {
        hash ~= cast(u32)b // Odin's bitwise XOR operator is ~
        hash *= FNV1A_PRIME_32
    }

    return hash
}
