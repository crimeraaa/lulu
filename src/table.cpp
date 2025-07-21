#include "table.hpp"
#include "vm.hpp"

static constexpr Entry
EMPTY_ENTRY = {nil, nil};

Table *
table_new(lulu_VM *vm, isize n_hash, isize n_array)
{
    Table *t = object_new<Table>(vm, &vm->objects, VALUE_TABLE);
    table_init(t);
    isize total = n_hash + n_array;
    if (total > 0) {
        table_resize(vm, t, mem_next_pow2(total));
    }
    return t;
}

void
table_init(Table *t)
{
    t->entries.data = nullptr;
    t->entries.len  = 0;
    t->count        = 0;
}

static isize
_table_cap(const Table *t)
{
    return len(t->entries);
}

template<class T>
static u32
_hash_any(T v)
{
    // Aliasing a `T` with a `char *` is defined behavior.
    LString s{cast(char *)&v, sizeof(T)};
    return hash_string(s);
}

static u32
_hash_value(Value v)
{
    switch (v.type()) {
    case VALUE_NIL:
        break;
    case VALUE_BOOLEAN:         return _hash_any(v.to_boolean());
    case VALUE_NUMBER:          return _hash_any(v.to_number());
    case VALUE_LIGHTUSERDATA:   return _hash_any(v.to_userdata());
    case VALUE_STRING:          return v.to_ostring()->hash;
    case VALUE_TABLE:           // fallthrough
    case VALUE_FUNCTION:        return _hash_any(v.to_object());
    case VALUE_INTEGER:
    case VALUE_CHUNK:
        break;
    }
    lulu_unreachable();
}


/**
 * @brief
 *  -   Finds the table entry with key matching `k`, or the first free entry.
 *
 * @note 2025-6-30
 *  Assumptions:
 *  1.) `len(entries)`, a.k.a. the table capacity, is non-zero at this point.
 *  1.1.) In `table_get()` we explicitly check if it's nonzero.
 *  1.2.) In `table_set()` we resize beforehand.
 */
static Entry *
_find_entry(Slice<Entry> entries, Value k)
{
    usize  hash = cast_usize(_hash_value(k));
    usize  wrap = cast_usize(len(entries)) - 1;
    Entry *tomb = nullptr;

    for (usize i = hash & wrap; /* empty */; i = (i + 1) & wrap) {
        Entry *entry = &entries[i];
        if (entry->key.is_nil()) {
            if (entry->value.is_nil()) {
                return (tomb == nullptr) ? entry : tomb;
            }
            // Track only the first tombstone we see so we can reuse it.
            if (tomb == nullptr) {
                tomb = entry;
            }
        }
        else if (k == entry->key) {
            return entry;
        }
    }
    lulu_unreachable();
}

bool
table_get(Table *t, Value k, Value *out)
{
    if (t->count > 0) {
        Entry *e = _find_entry(t->entries, k);
        *out = e->value;
        // If `e->key` is nil, then that means `k` does not exist in the table.
        return !e->key.is_nil();
    }
    *out = nil;
    return false;
}

void
table_set(lulu_VM *vm, Table *t, Value k, Value v)
{
    if (t->count + 1 > _table_cap(t)*3 / 4) {
        isize n = mem_next_pow2(t->count + 1);
        table_resize(vm, t, n);
    }
    Entry *e = _find_entry(t->entries, k);
    // Overwriting a completely empty entry?
    if (e->key.is_nil() && e->value.is_nil()) {
        t->count++;
    }
    e->key   = k;
    e->value = v;
}

void
table_unset(Table *t, Value k)
{
    if (t->count == 0) {
        return;
    }
    Entry *e = _find_entry(t->entries, k);
    // Already empty/tombstone; nothing to do.
    if (e->key.is_nil()) {
        return;
    }
    // Tombstones are nil keys mapping to the boolean `true`.
    e->key = nil;
    e->value.set_boolean(true);
}

void
table_resize(lulu_VM *vm, Table *t, isize new_cap)
{
    Slice<Entry> new_entries = slice_make<Entry>(vm, new_cap);
    // Initialize all key-value pairs to nil-nil.
    fill(new_entries, EMPTY_ENTRY);

    isize n = 0;
    // Rehash all the old elements into the new entries table.
    for (Entry e : t->entries) {
        // Skip empty entries and tombstones.
        if (e.key.is_nil()) {
            continue;
        }

        Entry *e2 = _find_entry(new_entries, e.key);
        e2->key   = e.key;
        e2->value = e.value;
        n++;
    }
    slice_delete(vm, t->entries);
    t->entries = new_entries;
    t->count   = n;
}
