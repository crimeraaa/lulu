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


table_new :: proc(vm: ^VM, n_array, n_hash: int) -> ^Table {
    t := object_new(Table, vm)
    if total := n_array + n_hash; total > 0 {
        adjust_capacity(t, total, vm.allocator)
    }
    return t
}

table_destroy :: proc(vm: ^VM, t: ^Table) {
    delete(t.entries, vm.allocator)
    t.count   = 0
    t.entries = nil
}

/*
Notes:
-   It is possible for `k` to exist and map to a nil value. This is valid.
-   But if `k` maps to nothing (that is, entry.key is nil, meaning we haven't
    yet mapped this key), then we can say it is invalid.
 */
table_get :: proc(t: ^Table, k: Value) -> (v: Value, ok: bool) #optional_ok {
    if t.count == 0 {
        return value_make(), false
    }

    entry := find_entry(t.entries, k)
    if ok = !value_is_nil(entry.key); ok {
        v = entry.value
    }
    return v, ok
}

table_set :: proc(vm: ^VM, t: ^Table, k, v: Value) {
    /*
    Notes:
    -   This is a safer version of the following line:

        `table->count > table->capacity > TABLE_MAX_LOAD`

        in Crafting Interpreters, Chapter 20.4.2: *Inserting Entries*.

    -   Where `TABLE_MAX_LOAD` is a macro that expands to `0.75`.
    -   Here we aim to reduce error by doing purely integer math.
    -   n*0.75 == n*(3/4) == (n*3)/4
     */
    if n := len(t.entries); t.count >= (n*3) / 4 {
        adjust_capacity(t, n, vm.allocator)
    }

    entry := find_entry(t.entries, k)
    // Tombstones have nil keys but non-nil values, so don't count them.
    if value_is_nil(entry.key) && value_is_nil(entry.value) {
        t.count += 1
    }
    entry.key   = k
    entry.value = v
}

table_unset :: proc(t: ^Table, k: Value) {
    if t.count == 0 {
        return
    }

    entry := find_entry(t.entries, k)
    if value_is_nil(entry.key) {
        return
    }

    // Tombstones are invalid keys with non-nil values.
    entry^ = Table_Entry{key = value_make(), value = value_make(true)}
}

/*
Analogous to:
-   `t.c:tableAddAll(Table *from, Table *to)`
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
find_entry :: proc(entries: []Table_Entry, k: Value) -> ^Table_Entry {

    get_hash :: proc(k: Value) -> (hash: u32) {
        switch k.type {
        case .Nil:      break
        case .Boolean:  return cast(u32)k.boolean
        case .Number:   return hash_f64(k.number)
        case .String:   return k.ostring.hash
        case .Table:    return hash_pointer(k.table)
        }
        unreachable("Cannot hash type %v", k.type)
    }

    wrap  := cast(u32)len(entries)
    tombstone: ^Table_Entry
    for i := get_hash(k) % wrap; /* empty */; i = (i + 1) % wrap {
        entry := &entries[i]
        // Tombstone or empty entry?
        if value_is_nil(entry.key) {
            // Empty entry?
            if value_is_nil(entry.value) {
                return entry if tombstone == nil else tombstone
            }
            // Non-nil value, so this is a tombstone.
            // Recycle the first one we see.
            if tombstone == nil {
                tombstone = entry
            }
        } else if value_eq(entry.key, k) {
            return entry
        }
    }
    unreachable("How did you even get here?")
}


@(private="file")
adjust_capacity :: proc(t: ^Table, new_cap: int, allocator: mem.Allocator) {
    /*
    Notes(2025-01-19):
    -   We add 1 because if `n` is a power of 2 already, we would return it!
     */
    new_cap := new_cap
    new_cap = max(8, math.next_power_of_two(new_cap + 1))

    // Assume all memory is zero'd out for us already. Fully zero'd = nil in Lua.
    new_entries := make([]Table_Entry, new_cap, allocator)
    new_count   := 0
    for old_entry in t.entries {
        // Skip tombstones and empty entries.
        if value_is_nil(old_entry.key) {
            continue
        }

        new_entry := find_entry(new_entries, old_entry.key)
        new_entry^ = Table_Entry{key = old_entry.key, value = old_entry.value}
        new_count += 1
    }

    delete(t.entries, allocator)
    t.entries = new_entries
    t.count   = new_count
}
