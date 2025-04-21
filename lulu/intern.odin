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

intern_init :: proc(vm: ^VM, interned: ^Intern) {
    interned.entries = make(map[string]^OString, vm.allocator)
}

/*
Notes:
-   All `^OString` are managed by the VM. DO NOT delete the entries manually.
-   It's entirely possible that a freed `^OString` will still be pointed to
    by some other non-string in the objects linked list: a dangling pointer.
 */
intern_destroy :: proc(interned: ^Intern) {
    delete(interned.entries)
}

intern_get :: proc(interned: ^Intern, key: string) -> (value: ^OString, ok: bool) {
    return interned.entries[key]
}

intern_set :: proc(interned: ^Intern, ostring: ^OString, location := #caller_location) {
    key := ostring_to_string(ostring)
    pkey, pvalue, just_inserted, mem_err := map_entry(&interned.entries, key)
    // We should only ever call this when inserting the key for the first time.
    assert(just_inserted, loc = location)
    assert(mem_err == nil)

    // Key and value effectively point to the same allocated memory.
    pkey^, pvalue^ = key, ostring
}

