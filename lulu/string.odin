#+private
package lulu

import "core:fmt"
import "core:io"
import "core:mem"
import "core:math"

/*
**Overview**
-   A variation of `Table` optimized specifically to intern strings.

- TODO(2025-05-27): Separate the list of strings from the main objects list.
 */
Intern :: struct {
    root:     ^Object,
    entries: []Intern_Entry, // len(entries) == allocated capacity
    count:     int,          // number of active entries
}

Intern_Entry :: ^OString

OString :: struct {
    using base: Object_Base,
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
    h := hash_string(input)
    if prev := intern_get(vm.interned, input, h); prev != nil {
        return prev
    }

    n := len(input)
    o := object_new(OString, vm, n + 1)
    object_link(&vm.interned.root, o)

    s := &o.ostring
    s.hash = h
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
    return hash_bytes(mem.ptr_to_bytes(&n))
}

hash_pointer :: #force_inline proc "contextless" (p: rawptr) -> (hash: u32) {
    p := p
    return hash_bytes(mem.ptr_to_bytes(&p))
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

@(private="file")
find_entry :: proc(entries: []Intern_Entry, o: ^OString) -> (p: ^Intern_Entry, is_first: bool) {
    wrap := u32(len(entries))
    tomb: ^Intern_Entry
    for i := o.hash % wrap; /* empty */; i = (i + 1) % wrap {
        p = &entries[i]
        if p^ == nil {
            is_first = tomb == nil
            return p if is_first else tomb, is_first
        }

        if .Collectible in p^.flags {
            // Reuse the first tombstone we see.
            if tomb == nil {
                tomb = p
            }
        } else if p^ == o {
            return p, false
        }
    }
    unreachable("How did you even get here?")
}

intern_get :: proc(t: Intern, s: string, hash: u32) -> (o: ^OString) {
    if t.count == 0 {
        return nil
    }

    entries := t.entries
    wrap    := u32(len(entries))
    for i := hash % wrap; /* empty */; i = (i + 1) % wrap {
        o = entries[i]
        if o == nil {
            return nil
        }
        // Check hash for early exit; compare strings only if we have to.
        if hash == o.hash && s == ostring_to_string(o) {
            // Tombstone- marked for collection but can now be re-used.
            if .Collectible in o.flags {
                o.flags -= {.Collectible}
            }
            return o
        }
    }
    return nil
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

intern_resize :: proc(vm: ^VM, t: ^Intern, n: int) {
    n := n
    n = max(8, math.next_power_of_two(n + 1))

    prev := t.entries
    defer slice_delete(vm, &prev)

    e2 := slice_make(vm, Intern_Entry, n)
    n2 := 0
    for e in prev {
        if e == nil {
            continue
        } else if .Collectible in e.flags {
            object_unlink(&t.root, cast(^Object)e)
            ostring_free(vm, e)
            continue
        }

        p, _ := find_entry(e2, e)
        p^ = e
        n2 += 1
    }

    t.entries = e2
    t.count   = n2
}

intern_unset :: proc(t: ^Intern, o: ^OString) {
    if t.count == 0 {
        return
    }

    entries := t.entries
    if p, _ := find_entry(entries, o); p != nil {
        p^.flags += {.Collectible}
        t.count -= 1
    }
}

intern_destroy :: proc(vm: ^VM, t: ^Intern) {
    iter := t.root
    for p in object_iterator(&iter) {
        ostring_free(vm, &p.ostring)
    }
    slice_delete(vm, &t.entries)
    t.count = 0
    t.root  = nil
}
