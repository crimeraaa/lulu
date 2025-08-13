#include "table.hpp"
#include "vm.hpp"

static const Entry
EMPTY_ENTRY_{nil, nil};

#define EMPTY_ENTRY     const_cast<Entry *>(&EMPTY_ENTRY_)

// Used primarily to ensure our hash part always has nonzero length
// when calling `table_get_entry()`.
static const Slice<Entry>
EMPTY_ENTRY_SLICE{EMPTY_ENTRY, 1};

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

static Entry *
find_entry(Slice<Entry> entries, Value k, usize start, usize stop)
{
    Entry *tomb = nullptr;

    // Don' reset `tomb` by this point; we may have wrapped around.
    loop_start:

    for (usize i = start; i < stop; i++) {
        Entry *e = &entries[i];
        // Nil key marks either an empty entry or a tombstone.
        if (e->key.is_nil()) {
            // Empty?
            if (e->value.is_nil()) {
                return (tomb == nullptr) ? e : tomb;
            }
            // Track only the first tombstone we see so we can reuse it.
            if (tomb == nullptr) {
                tomb = e;
            }
        } else if (k == e->key) {
            return e;
        }
    }
    // Failed to find up to this point so try the left side.
    if (start != 0) {
        stop = start;
        start = 0;
        goto loop_start;
    }
    return EMPTY_ENTRY;
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
 */
static Entry *
table_get_entry(Table *t, Value k)
{
    usize hash = cast_usize(hash_value(k));
    usize n = cast_usize(len(t->entries));
    lulu_assert(n > 0);
    // Bit-or 1 is to ensure that even if `n == 1` we don't end up with 0.
    usize wrap = (n - 1) | 1;

    // Try to find matching entry going rightward.
    Entry *e = find_entry(t->entries, k, /* start */ hash & wrap, /* stop */ n);
    return e;
}


/**
 * @note(2025-08-11)
 *      The previous entries array is not freed here because you may want
 *      to use it when rehashing.
 */
static void
table_hash_resize(lulu_VM *vm, Table *t, isize n)
{
    // Don't attempt to call `mem_next_pow2(0)` because we do not want
    // to grow in this case.
    if (n == 0) {
        t->entries = EMPTY_ENTRY_SLICE;
        t->count   = 0;
        return;
    }

    Slice<Entry> new_entries = slice_make<Entry>(vm, mem_next_pow2(n));
    // Initialize all key-value pairs to nil-nil.
    fill(new_entries, EMPTY_ENTRY_);
    t->entries = new_entries;
    t->count   = 0;
}

// Array indexes can only get so large.
static constexpr i32
MAX_INDEX_BITS = (INT32_WIDTH - 6),
MAX_INDEX = (1 << MAX_INDEX_BITS);

/**
 * @return
 *      The number of non-nil values in `t->array`. This is NOT equivalent
 *      to `#t`, because holes in the array are merely ignored rather than
 *      causing the count to stop immediately.
 */
static i32
table_array_count(Table *t, Slice<i32> index_ranges)
{
    i32 n_array_used = 0;
    // Iterator over all Lua indexes.
    i32 i = 1;
    // If array size is 0, the loop is never entered.
    for (i32 bit = 0, pow2 = 1; bit < MAX_INDEX_BITS; bit++, pow2 <<= 1) {
        i32 limit = pow2;
        // Clamp limit if iterating up to it would overflow the array.
        if (limit > len(t->array)) {
            limit = len(t->array);
            // No more elements to count at this point?
            if (i > limit) {
                break;
            }
        }
        // Counter for active array elements in this index range.
        // Count active array elements in the range (2^(bit - 1), 2^(bit)).
        i32 used = 0;
        for (/* empty */; i <= limit; i++) {
            Value v = t->array[i - 1];
            if (!v.is_nil()) {
                used++;
            }
        }
        index_ranges[bit] += used;
        n_array_used += used;
    }
    return n_array_used;
}

/**
 * @return
 *      `k` as an integer if it can represent one, else -1.
 */
static i32
array_index(Value k)
{
    if (k.is_number()) {
        Integer i = 0;
        if (number_to_integer(k.to_number(), &i)) {
            return cast(i32)i;
        }
    }
    return -1;
}

static i32
count_index(Value k, Slice<i32> index_ranges)
{
    i32 i = array_index(k);
    if (1 <= i && i <= MAX_INDEX) {
        double exp = log2(cast(double)i);
        // @todo(2025-08-11) Create dedicated integer `ceil(log2())` function.
        int bit = cast_int(ceil(exp));
        index_ranges[bit] += 1;
        return 1;
    }
    return 0;
}

static i32
table_hash_count_array(Table *t, Slice<i32> index_ranges)
{
    i32 n_array_extra = 0;
    for (Entry e : t->entries) {
        if (!e.key.is_nil()) {
            // No overflow because integer representation is checked for
            // its range in the maximums, we will never add more than
            // `MAX_INDEX` items.
            n_array_extra += count_index(e.key, index_ranges);
        }
    }
    return n_array_extra;
}

static i32
table_array_compute_size(Slice<i32> index_ranges, i32 *n_array_used)
{
    // Accumulator to track how many active indices are smaller than `pow2`.
    i32 acc = 0;
    i32 n_array_active = 0;
    i32 n_array_optimal = 0;

    for (i32 bit = 0, pow2 = 1; /* empty */; bit++, pow2 <<= 1) {
        i32 half = pow2 >> 1;
        if (half >= *n_array_used) {
            break;
        }

        // How many indices in this range are going to be active?
        i32 used = index_ranges[bit];
        if (used > 0) {
            acc += used;
            // More than half of all possible array elements up to this point
            // can fit?
            if (acc > half) {
                n_array_optimal = pow2;
                n_array_active = acc;
            }
        }

        // All elements already counted?
        if (acc == *n_array_used) {
            break;
        }
    }
    *n_array_used = n_array_optimal;
    lulu_assert(*n_array_used / 2 <= n_array_active && n_array_active <= *n_array_used);
    return n_array_active;
}

static void
table_array_resize(lulu_VM *vm, Table *t, isize n)
{
    isize last = len(t->array);
    n = mem_next_pow2(n);
    slice_resize(vm, &t->array, n);

    // Growing the array?
    if (n > last) {
        // Initialize the newly allocated region to all nils.
        fill(slice_from(t->array, last), nil);
    }
}

static void
table_resize(lulu_VM *vm, Table *t, isize n_hash, isize n_array)
{
    // Array must grow? Shrinking is a separate branch because we will
    // need to rehash the vanishing array slice *before* resizing.
    if (n_array > len(t->array)) {
        table_array_resize(vm, t, n_array);
    }

    Slice<Entry> old_entries = t->entries;
    table_hash_resize(vm, t, n_hash);
    // Array must shrink?
    if (n_array < len(t->array)) {
        // Copy here to avoid tripping up bounds check.
        Slice<Value> old_array = t->array;

        // Update len so that `table_set()` does not see the longer region.
        t->array.len = n_array;

        // Move elements from the vanishing array slice to the hash segment.
        for (isize i = n_array; i < len(old_array); i++) {
            Value v = old_array[i];
            if (!v.is_nil()) {
                table_set_integer(vm, t, i + 1, v);
            }
        }
        // Shrink the array allocation.
        table_array_resize(vm, t, n_array);
    }

    // Rehash all elements in the hash segment. This may also move integer
    // keys to the array segment.
    for (Entry e : old_entries) {
        if (!e.value.is_nil()) {
            table_set(vm, t, e.key, e.value);
        }
    }

    // Do we actually own the data?
    if (raw_data(old_entries) != EMPTY_ENTRY) {
        slice_delete(vm, old_entries);
    }
}

static void
table_rehash(lulu_VM *vm, Table *t, Value k)
{
    Array<i32, MAX_INDEX_BITS + 1> buf;
    Slice<i32> index_ranges = slice(buf);
    fill(index_ranges, 0);

    i32 n_array = table_array_count(t, index_ranges);
    isize n_total = n_array;
    // If rehashing from empty slice, don't count the empty entry.
    n_total += (raw_data(t->entries) == EMPTY_ENTRY ? 0 : len(t->entries));
    n_array += table_hash_count_array(t, index_ranges);

    // Add `k` to our counters.
    n_array += count_index(k, index_ranges);
    n_total += 1;

    i32 n_array_active = table_array_compute_size(index_ranges, &n_array);
    isize n_hash = n_total - n_array_active;
    table_resize(vm, t, n_hash, n_array);
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
table_delete(lulu_VM *vm, Table *t)
{
    if (raw_data(t->entries) != EMPTY_ENTRY) {
        slice_delete(vm, t->entries);
    }
    slice_delete(vm, t->array);
    mem_free(vm, t);
}

void
table_init(Table *t)
{
    t->array = {nullptr, 0};
    t->entries = EMPTY_ENTRY_SLICE;
    t->count = 0;
}

static Value *
table_array_ptr(Table *t, Integer i)
{
    if (1 <= i && i <= len(t->array)) {
        return &t->array[i - 1];
    }
    return nullptr;
}

static bool
table_hash_get(Table *t, Value k, Value *out)
{
    // Sentinel value is EMPTY_ENTRY which has a nil key and nil value.
    Entry *e = table_get_entry(t, k);
    bool found = !e->key.is_nil();
    *out = (found) ? e->value : nil;
    return found;
}

bool
table_get(Table *t, Value k, Value *out)
{
    Value *v = table_array_ptr(t, array_index(k));
    if (v != nullptr) {
        *out = *v;
        return true;
    }
    return table_hash_get(t, k, out);
}

static isize
table_is_full(Table *t)
{
    isize n = len(t->entries);
    // 0.75 load factor is only for hash cap greater than 8 (e.g. 16, 32).
    if (n > 8) {
        n = (n * 3) >> 2;
    }
    return t->count + 1 > n;
}


/**
 * @brief
 *      Implements `t[k] = v` assuming `k` could not be an array index.
 *      May rehash the table, mutually recursive with `table_set()`.
 *
 * @note(2025-08-11)
 *      Analogous to `ltable.c:newkey()` in Lua 5.1.5.
 */
static void
table_hash_set(lulu_VM *vm, Table *t, Value k, Value v)
{
    Entry *e = EMPTY_ENTRY;
    // Table still has free slots?
    if (!table_is_full(t)) {
        e = table_get_entry(t, k);
    }

    // Table is full; no more free slots remaining. Need to rehash.
    if (e == EMPTY_ENTRY) {
        table_rehash(vm, t, k);
        // k may be a valid array index now.
        table_set(vm, t, k, v);
        return;
    }

    // Entry is completely empty?
    if (e->key.is_nil() && e->value.is_nil()) {
        t->count++;
    }
    e->key = k;
    e->value = v;
}

void
table_set(lulu_VM *vm, Table *t, Value k, Value v)
{
    Value *dst = table_array_ptr(t, array_index(k));
    if (dst != nullptr) {
        *dst = v;
        return;
    }
    table_hash_set(vm, t, k, v);
}

//=== ARRAY MANIPULATION =============================================== {{{

isize
table_len(Table *t)
{
    // @todo(2025-08-11) Use binary search instead.
    isize i = 0;
    for (Value v : t->array) {
        if (v.is_nil()) {
            break;
        }
        i++;
    }

    // Potentially have indices in the hash part? This is mainly a concern
    // for indices larger than MAX_INDEX.
    if (i == len(t->array)) {
        Value v;
        while (table_get_integer(t, i + 1, &v)) {
            i++;
        }
    }
    return i;
}

bool
table_get_integer(Table *t, Integer i, Value *out)
{
    Value *v = table_array_ptr(t, i);
    if (v != nullptr) {
        *out = *v;
        return true;
    }
    // Index not in range of the array; try the hash part.
    Value k = Value::make_number(cast_number(i));
    return table_hash_get(t, k, out);
}


void
table_set_integer(lulu_VM *vm, Table *t, Integer i, Value v)
{
    Value *dst = table_array_ptr(t, i);
    if (dst != nullptr) {
        *dst = v;
        return;
    }
    // Index not in range of the array; try the hash part.
    Value k = Value::make_number(cast_number(i));
    table_hash_set(vm, t, k, v);
}

//=== }}} ==================================================================

void
table_unset(Table *t, Value k)
{
    Value *v = table_array_ptr(t, array_index(k));
    if (v != nullptr) {
        *v = nil;
        return;
    }

    Entry *e = table_get_entry(t, k);
    if (e != EMPTY_ENTRY) {
        e->set_tombstone();
    }
}


/**
 * @param k
 *      Either `nil` to signal the start of the iteration, or some value
 *      in `t->entries`. Note that calls to this function MUST return
 *      occupied indices in order; 1, 2, 3, 5, 7, etc.
 *
 * @return
 *      0 for the first iteration, else [0, #t) if in array, else
 *      i + #t + 1 if in hash.
 */
static isize
find_next(lulu_VM *vm, Table *t, Value k)
{
    // First iteration, always start at index 0.
    if (k.is_nil()) {
        return 0;
    }

    isize i = array_index(k);
    // `k` is represents an valid Lua array index?
    if (1 <= i && i <= len(t->array)) {
        // Next index. First iteration would have already poked at index 0.
        return (i - 1) + 1;
    }

    usize hash = cast_usize(hash_value(k));
    usize wrap = cast_usize(len(t->entries)) - 1;

    // The main index of `k` may be colliding; find its actual position
    for (usize i = hash & wrap; /* empty */; i = (i + 1) & wrap) {
        Entry e = t->entries[i];
        // `k` is not mapped to its ideal index nor a colliding chain.
        if (e.key.is_nil()) {
            break;
        } else if (e.key == k) {
            // Hash index of *next* element, adding #t to mark it as such.
            return cast_isize(i) + 1 + len(t->array);
        }
    }
    vm_runtime_error(vm, "Invalid key to 'next'");
    return 0;
}

bool
table_next(lulu_VM *vm, Table *t, Value *restrict k, Value *restrict v)
{
    // Find the index of the element after `k`, or 0 if starting out.
    isize i = find_next(vm, t, *k);
    for (/* empty */; i < len(t->array); i++) {
        Value src = t->array[i];
        if (!src.is_nil()) {
            *k = Value::make_number(cast_number(i + 1));
            *v = src;
            return true;
        }
    }

    // Hash 'indexes' were initially marked by `+ #t`; undo it.
    for (i -= len(t->array); i < len(t->entries); i++) {
        Entry e = t->entries[i];
        if (!e.key.is_nil()) {
            *k = e.key;
            *v = e.value;
            return true;
        }
    }
    // No more elements.
    return false;
}
