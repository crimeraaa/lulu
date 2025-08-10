#include "table.hpp"
#include "vm.hpp"

static constexpr Entry
EMPTY_ENTRY{nil, nil};

static isize
table_hash_cap(const Table *t)
{
    return len(t->entries);
}

template<class T>
static u32
hash_any(T v)
{
    // Aliasing a `T` with a `char *` is defined behavior.
    LString s{reinterpret_cast<const char *>(&v), sizeof(T)};
    return hash_string(s);
}

static u32
hash_value(Value v)
{
    switch (v.type()) {
    case VALUE_NIL:
        break;
    case VALUE_BOOLEAN:         return hash_any(v.to_boolean());
    case VALUE_NUMBER:          return hash_any(v.to_number());
    case VALUE_LIGHTUSERDATA:   return hash_any(v.to_userdata());
    case VALUE_STRING:          return v.to_ostring()->hash;
    case VALUE_TABLE:           [[fallthrough]];
    case VALUE_FUNCTION:        return hash_any(v.to_object());
    case VALUE_INTEGER:
    case VALUE_CHUNK:
        break;
    }
    lulu_panicf("Non-hashable Value_Type(%i)", v.type());
    lulu_unreachable();
}


/**
 * @brief
 *      Finds the table entry with key matching `k`, or the first free
 *      entry.
 *
 * @note(2025-6-30) Assumptions:
 *
 *  1.) `len(entries)`, a.k.a. the table capacity, is non-zero at this
 *      point.
 *
 *  1.1.) In `table_get()` we explicitly check if it's nonzero.
 *
 *  1.2.) In `table_set()` we resize beforehand.
 */
static Entry *
find_entry(Slice<Entry> &entries, Value k)
{
    usize  hash = cast_usize(hash_value(k));
    usize  wrap = cast_usize(len(entries)) - 1;
    Entry *tomb = nullptr;

    for (usize i = hash & wrap; /* empty */; i = (i + 1) & wrap) {
        Entry *e = &entries[i];
        if (e->is_empty_or_tombstone()) {
            // Empty?
            if (e->value.is_nil()) {
                return (tomb == nullptr) ? e : tomb;
            }
            // Track only the first tombstone we see so we can reuse it.
            if (tomb == nullptr) {
                tomb = e;
            }
        }
        else if (k == e->key) {
            return e;
        }
    }
    lulu_unreachable();
}

static void
table_hash_resize(lulu_VM *vm, Table *t, isize n)
{
    if (n == 0) {
        slice_delete(vm, t->entries);
        t->entries = {nullptr, 0};
        t->count   = 0;
        return;
    }

    Slice<Entry> new_entries = slice_make<Entry>(vm, mem_next_pow2(n));
    // Initialize all key-value pairs to nil-nil.
    fill(new_entries, EMPTY_ENTRY);

    n = 0;
    // Rehash all the old elements into the new entries table.
    for (Entry e : t->entries) {
        // Don't (re)map nil keys.
        if (e.is_empty_or_tombstone()) {
            continue;
        }

        Entry *e2 = find_entry(new_entries, e.key);
        e2->key   = e.key;
        e2->value = e.value;
        n++;
    }
    slice_delete(vm, t->entries);
    t->entries = new_entries;
    t->count   = n;
}

static bool
table_hash_get(Table *t, Value k, Value *out)
{
    if (t->count > 0) {
        Entry *e = find_entry(t->entries, k);
        *out = e->value;
        // If `e->key` is nil, then that means `k` does not exist in the table.
        return !e->is_empty_or_tombstone();
    }
    *out = nil;
    return false;
}

static void
table_hash_set(lulu_VM *vm, Table *t, Value k, Value v)
{
    isize n = table_hash_cap(t);
    if (t->count + 1 > (n * 3) / 4) {
        table_hash_resize(vm, t, n + 1);
    }

    Entry *e = find_entry(t->entries, k);
    // Overwriting a completely empty entry?
    if (e->is_empty()) {
        t->count++;
    }
    e->key   = k;
    e->value = v;
}

static isize
table_array_cap(Table *t)
{
    return len(t->array);
}

static isize
table_array_next_size(isize n)
{
    return mem_next_pow2(n);
}

static void
table_array_resize(lulu_VM *vm, Table *t, isize n)
{
    isize last = table_array_cap(t);
    n = table_array_next_size(n);
    slice_resize(vm, &t->array, n);

    // Growing the array?
    if (n > last) {
        // Initialize the newly allocated region to all nils.
        fill(slice_from(t->array, last), nil);
    }
}

Table *
table_new(lulu_VM *vm, isize n_hash, isize n_array)
{
    Table *t = object_new<Table>(vm, &vm->objects, VALUE_TABLE);
    table_init(t);
    if (n_hash > 0) {
        table_hash_resize(vm, t, n_hash);
    }
    if (n_array > 0) {
        table_array_resize(vm, t, n_array);
    }
    return t;
}

void
table_init(Table *t)
{
    t->array   = {nullptr, 0};
    t->entries = {nullptr, 0};
    t->count   = 0;
}

bool
table_get(Table *t, Value k, Value *out)
{
    if (k.is_number()) {
        Integer i;
        // `k` represents an integer without loss of precision?
        if (number_to_integer(k.to_number(), &i)) {
            return table_get_integer(t, i, out);
        }
    }
    return table_hash_get(t, k, out);
}

void
table_set(lulu_VM *vm, Table *t, Value k, Value v)
{
    if (k.is_number()) {
        Integer i;
        // `k` represents an integer without loss of precision?
        if (number_to_integer(k.to_number(), &i)) {
            table_set_integer(vm, t, i, v);
            return;
        }
    }
    table_hash_set(vm, t, k, v);
}

//=== ARRAY MANIPULATION =============================================== {{{

isize
table_len(Table *t)
{
    isize i = 0;
    for (const Value &v : t->array) {
        if (v.is_nil()) {
            break;
        }
        i++;
    }
    return i;
}

bool
table_get_integer(Table *t, Integer i, Value *out)
{
    // 1-based index from Lua is in range of array, when also treated as Lua?
    if (1 <= i && i <= table_array_cap(t)) {
        // Correct 1-based index from Lua to 0-based index for C.
        Value v = t->array[i - 1];
        *out = v;
        return !v.is_nil();
    }
    // Not in range of array segment; try hash segment.
    Value k = Value::make_number(cast_number(i));
    return table_hash_get(t, k, out);
}


/**
 * @note(2025-08-08) Assumptions:
 *
 * 1.) `t->count > 0`, because otherwise we have no active entires
 *      and potentially no entry array to begin with.
 */
static bool
moved_array(Table *t, Integer i)
{
    Value  k = Value::make_number(cast_number(i));
    Entry *e = find_entry(t->entries, k);
    if (e->key == k) {
        t->array[i - 1] = e->value;
        // Mark as tombstone so that it can be skipped over when resolving
        // collisions.
        e->set_tombstone();
        return true;
    }
    lulu_assertln(e->key == nil, "empty/tombstone keys can only be nil");
    return false;
}

// Count all consecutive, non-nil integer keys in the hash segment,
// starting from `start`.
static isize
hash_count_array(Table *t, Integer start)
{
    // No active hash elements, potentially no entry array to begin with!
    if (t->count == 0) {
        return 0;
    }

    isize n = 0;
    for (Integer i = start; /* empty */; i++) {
        Value  k = Value::make_number(cast_number(i));
        Entry *e = find_entry(t->entries, k);
        if (e->key == k) {
            n++;
            continue;
        }
        break;
    }
    return n;
}

/**
 * @brief
 *      Move all consecutive non-nil integer keys right before and right
 *      after `i` from the hash segment to the array segment.
 */
static void
hash_to_array(Table *t, Integer start, Integer stop)
{
    // Nothing to move?
    if (t->count == 0) {
        return;
    }

    // Move all consecutive non-nil integer hash keys in range `[1, i)`.
    for (Integer j = start - 1; j >= 1; j--) {
        // Could not remove from hash segment because it did not exist?
        if (!moved_array(t, j)) {
            break;
        }
    }

    // Move all consecutive non-nil integer hash keys in range `(i, n]`.
    for (Integer j = start + 1; j <= stop; j++) {
        if (!moved_array(t, j)) {
            break;
        }
    }
}

void
table_set_integer(lulu_VM *vm, Table *t, Integer i, Value v)
{
    // Valid 1-based index from Lua?
    if (1 <= i) {
        isize n = table_array_cap(t);
        // Is in range of the array?
        if (i <= n) {
            t->array[i - 1] = v;
            return;
        }

        // Check all integer keys to our right.
        isize extra = hash_count_array(t, i + 1);

        // Grow the array, accounting for the integer keys to the right.
        // @todo(2025-08-09) Count all active array indexes and shrink
        // if needed?
        n = table_array_next_size(n + 1) + extra;

        // Is in range of the potentially grown array?
        if (i <= n) {
            table_array_resize(vm, t, n);
            t->array[i - 1] = v;

            // Move integer indices from the hash segment to the array
            // segment.
            hash_to_array(t, i, i + cast_integer(extra));
            return;
        }
        // Not in range of array no matter what; use the hash segment.
    }
    Value k = Value::make_number(cast_number(i));
    table_hash_set(vm, t, k, v);
}

//=== }}} ==================================================================

void
table_unset(Table *t, Value k)
{
    if (t->count == 0) {
        return;
    }
    Entry *e = find_entry(t->entries, k);
    if (e->is_empty_or_tombstone()) {
        return;
    }
    e->set_tombstone();
}


/**
 * @param k
 *      Either `nil` to signal the start of the iteration, or some value
 *      in `t->entries`. Note that calls to this function MUST return
 *      occupied indices in order; 1, 2, 3, 5, 7, etc.
 */
static isize
find_next(lulu_VM *vm, Table *t, Value k)
{
    // First iteration, always start at index 0.
    if (k.is_nil()) {
        return 0;
    }

    usize hash = cast_usize(hash_value(k));
    usize wrap = cast_usize(len(t->entries)) - 1;

    // The main index of `k` may be colliding; find its actual position
    for (usize i = hash & wrap; /* empty */; i = (i + 1) & wrap) {
        Entry e = t->entries[i];
        if (e.is_empty_or_tombstone()) {
            break;
        } else if (e.key == k) {
            // Return index of *next* element.
            return cast_isize(i) + 1;
        }
    }
    vm_runtime_error(vm, "Invalid key to 'next'");
    return 0;
}

bool
table_next(lulu_VM *vm, Table *t, Value *restrict k, Value *restrict v)
{
    // Find the index of the element after `k`, or `0` if starting out.
    isize i = find_next(vm, t, *k);

    // Given this starting index, find the first non-nil element.
    for (; i < len(t->entries); i++) {
        Entry e = t->entries[i];
        if (!e.is_empty_or_tombstone()) {
            *k = e.key;
            *v = e.value;
            return true;
        }
    }
    // No more elements.
    return false;
}
