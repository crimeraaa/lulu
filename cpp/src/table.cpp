#include "table.hpp"
#include "vm.hpp"

static const Entry
EMPTY_ENTRY_{nil, nil};

#define EMPTY_ENTRY     const_cast<Entry *>(&EMPTY_ENTRY_)

// Used primarily to ensure our hash part always has nonzero length
// when calling `table_get_entry()`.
static const Slice<Entry>
EMPTY_ENTRY_SLICE{EMPTY_ENTRY, 1};

static u32
hash_boolean(bool b)
{
    u32 hash = FNV1A_OFFSET;
    hash ^= cast(u32)b;
    hash *= FNV1A_PRIME;
    return hash;
}

// Hashes 8-byte values as a pair of 4-bytes for performance.
template<class T>
static u32
hash_compound(T v)
{
    u32 hash = FNV1A_OFFSET;
    // Standards-compliant type punning. Optimizes to register moves.
    u32 buf[sizeof(v) / sizeof(hash)];
    memcpy(buf, &v, sizeof(buf));
    for (int i = 0; i < count_of(buf); i++) {
        hash ^= buf[i];
        hash *= FNV1A_PRIME;
    }
    return hash;
}

static u32
hash_value(Value v)
{
    switch (v.type()) {
    case VALUE_NIL:
        break;
    case VALUE_BOOLEAN:         return hash_boolean(v.to_boolean());
    case VALUE_NUMBER:          return hash_compound(v.to_number());
    case VALUE_LIGHTUSERDATA:   return hash_compound(v.to_userdata());
    case VALUE_STRING:          return v.to_ostring()->hash;
    case VALUE_TABLE:           [[fallthrough]];
    case VALUE_FUNCTION:        return hash_compound(v.to_object());
    case VALUE_INTEGER:
    case VALUE_CHUNK:
        break;
    }
    lulu_panicf("Non-hashable Value_Type(%i)", v.type());
    lulu_unreachable();
    return 0;
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
        t->count = 0;
        return;
    }

    // Minimum array size to prevent frequent reallocations.
    n = max(n, 8_i);
    n = mem_next_pow2(n);
    Slice<Entry> new_entries = slice_make<Entry>(vm, n);
    // Initialize all key-value pairs to nil-nil.
    fill(new_entries, EMPTY_ENTRY_);
    t->entries = new_entries;
    t->count = 0;
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
    i32 n_array = 0;
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
        // Count active array elements in the range (2^(bit-1), 2^(bit)].
        i32 used = 0;
        for (/* empty */; i <= limit; i++) {
            Value v = t->array[i - 1];
            if (!v.is_nil()) {
                used++;
            }
        }
        index_ranges[bit] += used;
        n_array += used;
    }
    return n_array;
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

/**
 * @brief
 *      Returns the *exponent* of the next power of 2 to `n`, if it is not
 *      one already.
 */
static u8
ceil_log2(u32 n)
{
    /**
     * @brief
     *      Map indices in the range [1, 256] to the index range exponents
     *      with which they fit at the end. Useful to determine
     *      the appropriate array size which `n` fits in.
     *
     * @note(2025-08-14)
     *      Index 0 is never a valid input, so this table actually
     *      maps `n - 1`.
     */
    static const u8 ceil_log2_lut[0x100] = {
        // [1, 1] => index_ranges[0]
        0,

        // [2, 2] => index_ranges[1]
        1,

        // [3, 4] => index_ranges[2]
        // Index 3 should map to bit 2 as given by ceil(log2(3)), NOT bit 1
        // as given by floor(log2(3)). Because when we calculate the optimal
        // array size, we want size of 4, not size of 2.
        2, 2,

        // [5, 8]
        3, 3, 3, 3,

        // [9, 16]
        4, 4, 4, 4, 4, 4, 4, 4,

        // [17, 32]
        5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,

        // [33, 64]
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
        6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,

        // [65, 128]
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
        7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,

        // [129, 256]
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
        8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    };

    // Accumulator for values of n that do not fit in the lookup table.
    // We know that if it does not fit, 2^8 is automatically added on top.
    // Concept check: ceil(log2(257))
    u8 acc = 0;
    while (n > 0x100) {
        n >>= 8;
        acc += 8;
    }
    return acc + ceil_log2_lut[n - 1];
}

static i32
count_index(Value k, Slice<i32> index_ranges)
{
    i32 i = array_index(k);
    if (1 <= i && i <= MAX_INDEX) {
        u8 bit = ceil_log2(cast(u32)i);
        index_ranges[bit] += 1;
        return 1;
    }
    return 0;
}


/**
 * @return
 *      Count of all valid array indices that are currently hashed.
 */
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


/**
 * @param n_array
 *      In-out-parameter. Initially holds the theoretical number of
 *      elements that goes to the array part, not yet accounting for
 *      extremely large gaps between indices.
 *
 *      When done, will instead hold the actual size of the array.
 *
 * @return
 *      The number of elements that will actually go into the array
 *      part.
 */
static i32
table_array_compute_size(Slice<i32> index_ranges, i32 *n_array)
{
    // Accumulator to track how many active indices are smaller than the
    // potential optimal size.
    i32 acc = 0;
    i32 n_array_active = 0;
    i32 n_array_optimal = 0;

    for (i32 bit = 0, pow2 = 1; /* empty */; bit++, pow2 <<= 1) {
        // Half of the potentially optimal size.
        i32 half = pow2 >> 1;

        // Our theoretical number of elements already fits?
        // Concept check: *n_array == 0.
        if (*n_array <= half) {
            break;
        }

        // How many indices in this range are going to be active?
        i32 used = index_ranges[bit];
        if (used > 0) {
            acc += used;
            // More than half of all array slots would be occupied?
            if (acc > half) {
                n_array_optimal = pow2;
                n_array_active = acc;
            }
        }

        // All elements already counted?
        if (acc == *n_array) {
            break;
        }
    }
    *n_array = n_array_optimal;
    lulu_assert(*n_array / 2 <= n_array_active && n_array_active <= *n_array);
    return n_array_active;
}

static void
table_array_resize(lulu_VM *vm, Table *t, isize n)
{
    isize last = len(t->array);

    // Minimum array size to prevent frequent reallocations.
    n = max(n, 8_i);
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
    // Copy here to avoid tripping up bounds check.
    Slice<Value> old_array = t->array;
    Slice<Entry> old_entries = t->entries;

    // Array must grow? Shrinking is a separate branch because we will
    // need to rehash the vanishing array slice *before* resizing.
    if (n_array > len(old_array)) {
        table_array_resize(vm, t, n_array);
    }

    table_hash_resize(vm, t, n_hash);

    // Array must shrink?
    if (n_array < len(old_array)) {
        // Update len so that `table_set()` does not see the longer region.
        t->array.len = n_array;

        // Move elements from the vanishing array slice to the hash segment.
        for (isize i = n_array, n = len(old_array); i < n; i++) {
            Value v = old_array[i];
            if (!v.is_nil()) {
                table_set_integer(vm, t, i + 1, v);
            }
        }
        // Shrink the array allocation.
        table_array_resize(vm, t, n_array);
    }

    /**
     * @brief
     *      Rehash all elements in the hash segment. This may also move
     *      integer keys to the array segment.
     *
     * @todo(2025-08-14)
     *      If new hash length is the same, don't resize. Figure out how
     *      to rehash the same old data.
     */
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
        // Array slot is occupied?
        return !v->is_nil();
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
 *      However we guarantee that, by that point there is already
 *      a free array index or free hash slot.
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

    // May have remaining integer keys in the hash part?
    // e.g. #array == 4 but we hashed k = 5 because #hash >= 8.
    if (i == len(t->array)) {
        // Don't call table_get*() because we already know this key
        // is not in the hash segment.
        for (;;) {
            Value k = Value::make_number(cast_number(i + 1));
            Value v;
            if (!table_hash_get(t, k, &v)) {
                break;
            }
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
        // Array slot is occupied?
        return !v->is_nil();
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
