#+private
package lulu

import "core:math"

Table :: struct {
    using base: Object_Base,
    entries:    []Table_Entry, // `len(entries)` == allocated capacity.
    count:      int, // Number of active entries. Not necessarily contiguous!
}

Table_Entry :: struct {
    key, value: Value,
}


table_new :: proc(vm: ^VM, n_array, n_hash: int) -> ^Table {
    o := object_new(Table, vm)
    object_link(&vm.objects, o)

    t := &o.table
    if total := n_array + n_hash; total > 0 {
        table_resize(vm, t, total)
    }
    return t
}

table_destroy :: proc(vm: ^VM, t: ^Table) {
    slice_delete(vm, &t.entries)
    t.count   = 0
    t.entries = nil
}

/*
**Notes**:
-   It is possible for `k` to exist and map to a nil value. This is valid.
-   But if `k` maps to nothing (that is, entry.key is nil, meaning we haven't
    yet mapped this key), then we can say it is invalid.

**Assumptions**
-   `k` is non-nil.
-   We assume the VM handled that beforehand, it would have thrown an error.
 */
table_get :: proc(t: Table, k: Value) -> (v: Value, ok: bool) #optional_ok {
    if t.count == 0 {
        return value_make(), false
    }

    entry := find_entry(t.entries, k)
    if !value_is_nil(entry.key) {
        v = entry.value
    }
    return v, true
}


/*
**Assumptions**
-   `k` is non-nil.
-   We assume the VM handled that beforehand, it would have thrown an error.
-   If `v` is nil then we proceed with mapping it as the value of `k`; however
    this entry will be considered a tombstone.

**Notes**
-   Concept check:
    ```lua
    local t = {}
    t.a = nil
    for k, v in pairs(t) do
        print(k, v)
    end
    ```
 */
table_set :: proc(vm: ^VM, t: ^Table, k, v: Value) {
    /*
    **Notes**
    -   This is a safer version of the following line:

        `table->count > table->capacity > TABLE_MAX_LOAD`

        in Crafting Interpreters, Chapter 20.4.2: *Inserting Entries*.

    -   Where `TABLE_MAX_LOAD` is a macro that expands to `0.75`.
    -   Here we aim to reduce error by doing purely integer math.
    -   n*0.75 == n*(3/4) == (n*3)/4
     */
    if n := len(t.entries); t.count >= (n*3) / 4 {
        table_resize(vm, t, n)
    }

    entry := find_entry(t.entries, k)

    // Don't count tombstones as they were valid at some point and thus added
    // to the count already.
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

    // If unset or a tombstone, it's already unset.
    if entry := find_entry(t.entries, k); !value_is_nil(entry.key) {
        entry^ = Table_Entry{key = value_make(), value = value_make(true)}
    }
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


/*
**Assumptions**
-   `k` is non-nil. That's all I ask.
 */
@(private="file")
find_entry :: proc(entries: []Table_Entry, k: Value) -> ^Table_Entry {

    get_hash :: proc(k: Value) -> u32 {
        switch k.type {
        case .None, .Nil: break
        case .Boolean:    return u32(k.boolean)
        case .Number:     return hash_number(k.number)
        case .String:     return k.ostring.hash
        case .Table, .Function:
            return hash_pointer(k.object)
        }
        unreachable("Cannot hash type %v", k.type)
    }

    wrap := cast(u32)len(entries)
    tomb: ^Table_Entry
    for i := get_hash(k) % wrap; /* empty */; i = (i + 1) % wrap {
        entry := &entries[i]
        // Tombstone or empty entry?
        if value_is_nil(entry.key) {
            // Empty entry?
            if value_is_nil(entry.value) {
                return entry if tomb == nil else tomb
            }
            // Non-nil value, so this is a tombstone.
            // Recycle the first one we see.
            if tomb == nil {
                tomb = entry
            }
        } else if value_eq(entry.key, k) {
            return entry
        }
    }
    unreachable("How did you even get here?")
}


table_resize :: proc(vm: ^VM, t: ^Table, n: int) {
    /*
    Notes(2025-01-19):
    -   We add 1 because if `n` is a power of 2 already, we would return it!
     */
    n := n
    n = max(8, math.next_power_of_two(n + 1))

    prev := t.entries
    defer slice_delete(vm, &prev)

    // Assume all memory is zero'd out for us already. Fully zero'd = nil in Lua.
    next := slice_make(vm, Table_Entry, n)
    next_n := 0
    for old in prev {
        // Skip tombstones and empty entries.
        if value_is_nil(old.key) {
            continue
        }

        e := find_entry(next, old.key)
        e^ = Table_Entry{key = old.key, value = old.value}
        next_n += 1
    }

    t.entries = next
    t.count   = next_n
}
