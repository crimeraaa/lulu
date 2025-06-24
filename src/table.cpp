#include "table.hpp"
#include "vm.hpp"

Table *
table_new(lulu_VM &vm, size_t n)
{
    Table *t = object_new<Table>(vm, &vm.objects, VALUE_TABLE);
    table_init(*t);
    if (n > 0) {
        table_resize(vm, *t, n);
    }
    return t;
}

void
table_init(Table &t)
{
    t.entries.data = nullptr;
    t.entries.len  = 0;
    t.count        = 0;
}

static size_t
table_cap(const Table &t)
{
    return len(t.entries);
}

template<class T>
static u32
hash_any(T v)
{
    // Can't use union approach; is undefined in C++ (but not C).
    char buf[sizeof(T)];
    memcpy(buf, &v, sizeof(T));
    return hash_string(String(buf, sizeof(buf)));
}

static u32
hash_value(Value v)
{
    switch (v.type) {
    case VALUE_NIL:
        break;
    case VALUE_BOOLEAN:  return hash_any(value_to_boolean(v));
    case VALUE_NUMBER:   return hash_any(value_to_number(v));
    case VALUE_STRING:   return value_to_ostring(v)->hash;
    case VALUE_TABLE:
    case VALUE_FUNCTION: return hash_any(value_to_object(v));
    case VALUE_CHUNK:
        break;
    }
    lulu_unreachable();
}

static Entry &
find_entry(Slice<Entry> entries, Value k)
{
    size_t hash = cast_size(hash_value(k));
    size_t wrap = len(entries) - 1;
    Entry *tomb = nullptr;

    for (size_t i = hash & wrap; /* empty */; i = (i + 1) & wrap) {
        Entry &entry = entries[i];
        if (value_is_nil(entry.key)) {
            if (value_is_nil(entry.value)) {
                return (tomb == nullptr) ? entry : *tomb;
            }
            // Track only the first tombstone we see so we can reuse it.
            if (tomb == nullptr) {
                tomb = &entry;
            }
        }
        else if (entry.key == k) {
            return entry;
        }
    }
    lulu_unreachable();
}

Table_Result
table_get(Table &t, Value k)
{
    if (t.count > 0) {
        Entry &e = find_entry(t.entries, k);
        // If `e->key` is nil, then that means `k` does not exist in the table.
        return {e.value, !value_is_nil(e.key)};
    }
    return {Value(), false};
}

void
table_set(lulu_VM &vm, Table &t, Value k, Value v)
{
    if (t.count + 1 > table_cap(t)*3 / 4) {
        size_t n = mem_next_size(t.count + 1);
        table_resize(vm, t, n);
    }
    Entry &e = find_entry(t.entries, k);
    // Overwriting a completely empty entry?
    if (value_is_nil(e.key) && value_is_nil(e.value)) {
        t.count++;
    }
    e.key   = k;
    e.value = v;
}

void
table_unset(Table &t, Value k)
{
    if (t.count == 0) {
        return;
    }
    Entry &e = find_entry(t.entries, k);
    // Already empty/tombstone; nothing to do.
    if (value_is_nil(e.key)) {
        return;
    }
    // Tombstones are nil keys mapping to the boolean `true`.
    e.key   = Value();
    e.value = Value(true);
}

void
table_resize(lulu_VM &vm, Table &t, size_t new_cap)
{
    Slice<Entry> new_entries = slice_make<Entry>(vm, new_cap);
    for (Entry &e : new_entries) {
        e.key   = Value();
        e.value = Value();
    }

    size_t n = 0;
    // Rehash all the old elements into the new entries table.
    for (Entry e : t.entries) {
        // Skip empty entries and tombstones.
        if (value_is_nil(e.key)) {
            continue;
        }

        Entry &e2 = find_entry(new_entries, e.key);
        e2.key    = e.key;
        e2.value  = e.value;
        n++;
    }
    slice_delete(vm, t.entries);
    t.entries = new_entries;
    t.count   = n;
}
