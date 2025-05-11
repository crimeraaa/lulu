#+private
package lulu

/*
Notes:
-   Since `string` is a pointer and length pair, memory in the map is allocated
    to hold those, not the data being pointed to.
-   We save memory by allocating the data for the `OString`, and have both the
    key and value point to it.

Links:
-   https://pkg.odin-lang.org/core/strings/#Intern
*/
Intern :: struct {
    entries: map[string]^OString,
}

intern_init :: proc(vm: ^VM, i: ^Intern) {
    i.entries = make(map[string]^OString, vm.allocator)
}

/*
Notes:
-   All `^OString` are managed by the VM. DO NOT delete the entries manually.
-   It's entirely possible that a freed `^OString` will still be pointed to
    by some other non-string in the objects linked list: a dangling pointer.
 */
intern_destroy :: proc(i: ^Intern) {
    delete(i.entries)
}

intern_get :: proc(i: ^Intern, key: string) -> (value: ^OString, ok: bool) {
    return i.entries[key]
}

intern_set :: proc(i: ^Intern, s: ^OString, location := #caller_location) {
    key := ostring_to_string(s)
    pkey, pvalue, just_inserted, mem_err := map_entry(&i.entries, key)
    // We should only ever call this when inserting the key for the first time.
    assert(just_inserted, loc = location)
    assert(mem_err == nil)

    // Key and value effectively point to the same allocated memory.
    pkey^, pvalue^ = key, s
}

