#+private
package lulu

import "core:fmt"
import "core:io"
import "core:mem"
import "core:math"

/* 
**Overview**
-   A variation of `Table` optimized specifically to intern strings.
 */
Intern :: struct {
    entries: []Intern_Entry, // len(entries) == allocated capacity
    count:     int,     // number of active entries
}

Intern_Entry :: ^OString

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
    if prev := intern_get(vm.interned, input); prev != nil {
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
    intern_set(vm, &vm.interned, s)
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

// Some unique, non-nil address that we will never write to.
@(private="file")
TOMBSTONE := OString{}

@(private="file")
find_entry :: proc(entries: []Intern_Entry, s: $T) -> (p: ^Intern_Entry, is_first: bool)
where T == string || T == ^OString #optional_ok {
    wrap := u32(len(entries))
    tomb: ^Intern_Entry
    hash := hash_string(s) when T == string else s.hash
    for i := hash % wrap; /* empty */; i = (i + 1) % wrap {
        p = &entries[i]
        switch p^ {
        case nil:
            is_first = tomb == nil
            return p if is_first else tomb, is_first
        case &TOMBSTONE:
            if tomb == nil {
                tomb = p
            }
            continue
        case:
            last := ostring_to_string(p^) when T == string else p^
            if last == s {
                return p, false
            }
        }
    }
    unreachable("How did you even get here?")
}

intern_get :: proc(t: Intern, s: string) -> (o: ^OString) {
    if t.count == 0 {
        return
    }
    p := find_entry(t.entries, s)
    return p^ if p != nil else nil
}

intern_set :: proc(vm: ^VM, t: ^Intern, o: ^OString) {
    if n := len(t.entries); t.count >= (n*3) / 4 {
        intern_resize(vm, t, n)
    }
    p, is_first := find_entry(t.entries, o)
    p^ = o
    // Tombstones would have already been counted.
    if is_first {
        t.count += 1
    }
}

intern_resize :: proc(vm: ^VM, t: ^Intern, new_cap: int) {
    new_cap := new_cap
    new_cap = max(8, math.next_power_of_two(new_cap + 1))
    
    prev := t.entries
    defer slice_delete(vm, &prev)

    t.entries = slice_make(vm, Intern_Entry, new_cap)
    t.count   = 0
    for e in prev {
        if e == nil || e == &TOMBSTONE {
            continue
        }
        
        p := find_entry(t.entries, e)
        p^ = e
        t.count += 1
    }
}

intern_unset :: proc(t: ^Intern, o: ^OString) {
    if t.count == 0 {
        return
    }

    if p := find_entry(t.entries, o); p != nil {
        p^ = &TOMBSTONE
        t.count -= 1
    }
}

intern_destroy :: proc(vm: ^VM, t: ^Intern) {
    slice_delete(vm, &t.entries)
    t.count = 0
}
