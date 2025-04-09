#+private
package lulu

import "core:math"
import "core:mem"

Table :: struct {
    using base: Object_Header,
    entries:    []Table_Entry, // `len(entries)` == allocated capacity.
    count:      int, // Number of active entries. Not necessarily contiguous!
}

Table_Entry :: struct {
    key, value: Value,
}

table_new :: proc(vm: ^VM) -> ^Table {
    table := object_new(Table, vm)
    return table
}

table_destroy :: proc(vm: ^VM, table: ^Table) {
    delete(table.entries, vm.allocator)
    table.count   = 0
    table.entries = nil
}

/*
Notes:
-   It is possible for `key` to exist and map to a nil value. This is valid.
-   But if `key` maps to nothing (that is, entry.key is nil, meaning we haven't
    yet mapped this key), then we can say it is invalid.
 */
table_get :: proc(table: ^Table, key: Value) -> (value: Value, valid: bool) {
    if table.count == 0 {
        value_set_nil(&value)
        return value, false
    }

    entry := find_entry(table.entries, key)
    if valid = !value_is_nil(entry.key); valid {
        value = entry.value
    }
    return value, valid
}

table_set :: proc(vm: ^VM, table: ^Table, key, value: Value) {
    /*
    Notes:
    -   This is a safer version of line `table->count > table->capacity > TABLE_MAX_LOAD`
        in the book.
    -   Where `TABLE_MAX_LOAD` is a macro that expands to `0.75`.
    -   Here we aim to reduce error by doing purely integer math.
    -   n*0.75 == n*(3/4) == (n*3)/4
     */
    if n := len(table.entries); table.count >= (n*3) / 4 {
        /*
        Notes(2025-01-19):
        -   We add 1 because if `n` is a power of 2 already, we would return it!
         */
        new_cap := max(8, math.next_power_of_two(n + 1))
        adjust_capacity(table, new_cap, vm.allocator)
    }

    entry := find_entry(table.entries, key)
    // Tombstones have nil keys but non-nil values, so don't count them.
    if value_is_nil(entry.key) && value_is_nil(entry.value) {
        table.count += 1
    }
    entry.key   = key
    entry.value = value
}

table_unset :: proc(table: ^Table, key: Value) {
    if table.count == 0 {
        return
    }

    entry := find_entry(table.entries, key)
    if value_is_nil(entry.key) {
        return
    }

    // Tombstones are invalid keys with non-nil values.
    value_set_nil(&entry.key)
    value_set_boolean(&entry.value, true)
}

/*
Analogous to:
-   `table.c:tableAddAll(Table *from, Table *to)`
 */
table_copy :: proc(vm: ^VM, dst: ^Table, src: Table) {
    for entry in src.entries {
        // Skip tombstones and empty entries.
        if value_is_nil(entry.key) {
            continue
        }
        table_set(vm, dst, entry.key, entry.value)
    }
}

@(private="file")
find_entry :: proc(entries: []Table_Entry, key: Value) -> ^Table_Entry {
    wrap  := cast(u32)len(entries)
    tombstone: ^Table_Entry
    for index := get_hash(key) % wrap; /* empty */; index = (index + 1) % wrap {
        entry := &entries[index]
        // Tombstone or empty entry?
        if value_is_nil(entry.key) {
            // Empty entry?
            if value_is_nil(entry.value) {
                return entry if tombstone == nil else tombstone
            }
            // Non-nil value, so this is a tombstone. Recycle the first one we see.
            if tombstone == nil {
                tombstone = entry
            }
        } else if value_eq(entry.key, key) {
            return entry
        }
    }
    unreachable()
}

@(private="file")
get_hash :: proc(key: Value) -> (hash: u32) {
    switch key.type {
    case .Nil:      unreachable()
    case .Boolean:  return cast(u32)key.boolean
    case .Number:   return hash_f64(key.number)
    case .String:   return key.ostring.hash
    case .Table:    return hash_pointer(key.table)
    }
    unreachable()
}

@(private="file")
adjust_capacity :: proc(table: ^Table, new_cap: int, allocator: mem.Allocator) {
    // Assume all memory is zero'd out for us already. Fully zero'd = nil in Lua.
    new_entries := make([]Table_Entry, new_cap, allocator)
    new_count := 0
    for old_entry in table.entries {
        // Skip tombstones and empty entries.
        if value_is_nil(old_entry.key) {
            continue
        }

        new_entry := find_entry(new_entries, old_entry.key)
        value_copy(&new_entry.key,   old_entry.key)
        value_copy(&new_entry.value, new_entry.value)
        new_count += 1
    }

    delete(table.entries, allocator)
    table.entries = new_entries
    table.count   = new_count
}
