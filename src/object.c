#include "object.h"
#include "limits.h"
#include "memory.h"
#include "vm.h"

#define prepend_to_list(head, node) \
    ((node)->next = (head), (head) = (node))

#define remove_from_list(head, node) \
    ((head) = (node)->next)

// Separate name from the macro since `allocate_flexarray` also needs access.
static Object *_allocate_object(VM *vm, size_t size, VType tag) {
    Object *object = reallocate(vm, NULL, 0, size);
    object->tag    = tag;
    return prepend_to_list(vm->objects, object);
}

#define allocate_object(vm, T, tag) \
    (T*)_allocate_object(vm, sizeof(T), tag)

#define allocate_flexarray(vm, ST, MT, N, tag) \
    (ST*)_allocate_object(vm, flexarray_size(ST, MT, N), tag)

#define allocate_tstring(vm, N) \
    allocate_flexarray(vm, TString, char, N, TYPE_STRING)

const char *const LULU_TYPENAMES[] = {
    [TYPE_NIL]     = "nil",
    [TYPE_BOOLEAN] = "boolean",
    [TYPE_NUMBER]  = "number",
    [TYPE_STRING]  = "string",
};

void print_value(const TValue *self) {
    switch (get_tagtype(self)) {
    case TYPE_NIL:
        printf("nil");
        break;
    case TYPE_BOOLEAN:
        printf(as_boolean(self) ? "true" : "false");
        break;
    case TYPE_NUMBER:
        printf(NUMBER_FMT, as_number(self));
        break;
    case TYPE_STRING:
        printf("%s", as_cstring(self));
        break;
    }
}

bool values_equal(const TValue *lhs, const TValue *rhs) {
    // Logically, differing types can never be equal.
    if (get_tagtype(lhs) != get_tagtype(rhs)) {
        return false;
    }
    // For objects we assume they are correctly interned.
    switch (get_tagtype(lhs)) {
    case TYPE_NIL:      return true;
    case TYPE_BOOLEAN:  return as_boolean(lhs) == as_boolean(rhs);
    case TYPE_NUMBER:   return num_eq(as_number(lhs), as_number(rhs));
    case TYPE_STRING:   return as_object(lhs) == as_object(rhs);
    }
}

void init_tarray(TArray *self) {
    self->values = NULL;
    self->len = 0;
    self->cap = 0;
}

void free_tarray(VM *vm, TArray *self) {
    free_array(vm, TValue, self->values, self->len);
    init_tarray(self);
}

void write_tarray(VM *vm, TArray *self, const TValue *value) {
    if (self->len + 1 > self->cap) {
        int oldcap   = self->cap;
        self->cap    = grow_capacity(oldcap);
        self->values = grow_array(vm, TValue, self->values, oldcap, self->cap);
    }
    self->values[self->len] = *value;
    self->len++;
}

// TSTRING MANAGEMENT ----------------------------------------------------- {{{1

// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
#define FNV1A_PRIME32   0x01000193
#define FNV1A_OFFSET32  0x811c9dc5
#define FNV1A_PRIME64   0x00000100000001B3
#define FNV1A_OFFSET64  0xcbf29ce484222325

// NOTE: For `concat_string` we do not know the correct hash yet.
static TString *allocate_string(VM *vm, int len) {
    TString *inst = allocate_tstring(vm, len + 1);
    inst->len = len;
    return inst;
}

static char get_escape(char ch) {
    switch (ch) {
    case '0':   return '\0';
    case 'a':   return '\a';
    case 'b':   return '\b';
    case 'f':   return '\f';
    case 'n':   return '\n';
    case 'r':   return '\r';
    case 't':   return '\t';
    case 'v':   return '\v';

    case '\\':  return '\\';
    case '\'':  return '\'';
    case '\"':  return '\"';
    default:    return ch;   // TODO: Warn user? Throw error?
    }
}

// Will hash escape sequences correctly.
static uint32_t hash_string(const char *data, int len) {
    uint32_t hash = FNV1A_OFFSET32;
    char prev = 0;
    for (int i = 0; i < len; i++) {
        char ch = data[i];
        if (ch == '\\' && prev != '\\') {
            prev = ch;
            continue;
        }
        if (prev == '\\') {
            hash ^= cast(Byte, get_escape(ch));
            prev = 0;
        } else {
            hash ^= cast(Byte, ch);
        }
        hash *= FNV1A_PRIME32;
    }
    return hash;
}

static void build_string(TString *self, const char *src, int len, uint32_t hash) {
    char *end   = self->data; // For loop counter may skip.
    int skips   = 0;          // Number escape characters emitted.
    char prev   = 0;
    for (int i = 0; i < len; i++) {
        char ch = src[i];
        // Handle `"\\"` appropriately.
        if (ch == '\\' && prev != '\\') {
            skips++;
            prev = ch;
            continue;
        }
        // TODO: 3-digit number literals (0-prefixed), 2-digit ASCII codes
        if (prev == '\\') {
            *end = get_escape(ch);
            prev   = 0;
        } else {
            *end = ch;
        }
        end++;
    }
    *end = '\0';
    self->len  = len - skips;
    self->hash = hash;
}

TString *copy_string(VM *vm, const char *literal, int len) {
    uint32_t hash = hash_string(literal, len);
    TString *interned = find_interned(vm, literal, len, hash);

    // Is this string already interned?
    if (interned != NULL) {
        return interned;
    }

    TString *inst = allocate_string(vm, len);
    build_string(inst, literal, len, hash);

    // If we have escapes, are we really REALLY sure this isn't interned?
    if (inst->len != len) {
        interned = find_interned(vm, inst->data, inst->len, inst->hash);
        if (interned != NULL) {
            // Remove `inst` from the allocation list as it will be freed here.
            remove_from_list(vm->objects, &inst->object);
            deallocate_tstring(vm, inst);
            return interned;
        }
    }
    set_table(vm, &vm->strings, &make_string(inst), &make_boolean(true));
    return inst;
}

TString *concat_strings(VM *vm, const TString *lhs, const TString *rhs) {
    int len = lhs->len + rhs->len;
    TString *inst = allocate_string(vm, len);

    // Don't use `build_string` as it would have already been called for both.
    memcpy(inst->data,            lhs->data, lhs->len);
    memcpy(inst->data + lhs->len, rhs->data, rhs->len);
    inst->data[len] = '\0';
    inst->hash = hash_string(inst->data, len);

    TString *interned = find_interned(vm, inst->data, inst->len, inst->hash);
    if (interned != NULL) {
        // Remove `inst` from the allocation list as it will be freed here.
        remove_from_list(vm->objects, &inst->object);
        deallocate_tstring(vm, inst);
        return interned;
    }
    return inst;
}

// 1}}} ------------------------------------------------------------------------

// TABLE MANAGEMENT ------------------------------------------------------- {{{1

#define TABLE_MAX_LOAD  0.75

void init_table(Table *self) {
    self->entries = NULL;
    self->count   = 0;
    self->cap     = 0;
}

void free_table(VM *vm, Table *self) {
    free_array(vm, Entry, self->entries, self->count);
    init_table(self);
}

// WARNING: Assumes `Number` is `double` and is the same size as `uint64_t`!
static uint32_t hash_number(Number number) {
    union {
        Number   data;
        uint64_t bits;
    } hash;
    hash.data = number;
    return hash.bits % UINT32_MAX;
}

static uint32_t hash_value(const TValue *self) {
    switch (get_tagtype(self)) {
    case TYPE_NIL:      return 0; // WARNING: We should never hash `nil`!
    case TYPE_BOOLEAN:  return as_boolean(self);
    case TYPE_NUMBER:   return hash_number(as_number(self));
    case TYPE_STRING:   return as_string(self)->hash;
    }
}

// Find a free slot. Assumes there is at least 1 free slot left.
static Entry *find_entry(Entry *self, int cap, const TValue *key) {
    uint32_t index = hash_value(key) % cap;
    Entry *tombstone = NULL;
    for (;;) {
        Entry *entry = &self[index];
        if (is_nil(&entry->key)) {
            if (is_nil(&entry->value)) {
                return (tombstone == NULL) ? entry : tombstone;
            } else if (tombstone == NULL) {
                tombstone = entry;
            }
        } else if (values_equal(&entry->key, key)) {
            return entry;
        }
        index = (index + 1) % cap;
    }
}

// Analogous to `adjustCapacity()` in the book.
static void resize_table(VM *vm, Table *self, int newcap) {
    Entry *newbuf = allocate(vm, Entry, newcap);
    for (int i = 0; i < newcap; i++) {
        set_nil(&newbuf[i].key);
        set_nil(&newbuf[i].value);
    }

    // Copy non-empty and non-tombstone entries to the new table.
    self->count = 0;
    for (int i = 0; i < self->cap; i++) {
        Entry *src = &self->entries[i];
        if (is_nil(&src->key)) {
            continue; // Throws away both empty and tombstone entries.
        }
        Entry *dst = find_entry(newbuf, newcap, &src->key);
        dst->key   = src->key;
        dst->value = src->value;
        self->count++;
    }
    free_array(vm, Entry, self->entries, self->cap);
    self->entries = newbuf;
    self->cap     = newcap;
}

bool get_table(VM *vm, Table *self, const TValue *key, TValue *out) {
    unused(vm);
    if (self->count == 0) {
        return false;
    }
    Entry *entry = find_entry(self->entries, self->cap, key);
    if (is_nil(&entry->key)) {
        return false;
    }
    *out = entry->value;
    return true;
}

bool set_table(VM *vm, Table *self, const TValue *key, const TValue *value) {
    if (self->count + 1 > self->cap * TABLE_MAX_LOAD) {
        resize_table(vm, self, grow_capacity(self->cap));
    }
    Entry *entry  = find_entry(self->entries, self->cap, key);
    bool isnewkey = is_nil(&entry->key); // All keys implicitly nil by default.

    // Don't increase the count for tombstones (nil-key with non-nil value)
    if (isnewkey && is_nil(&entry->value)) {
        self->count++;
    }
    entry->key   = *key;
    entry->value = *value;
    return isnewkey;
}

bool unset_table(VM *vm, Table *self, const TValue *key) {
    unused(vm);
    if (self->count == 0) {
        return false;
    }
    Entry *entry = find_entry(self->entries, self->cap, key);
    if (is_nil(&entry->key)) {
        return false;
    }
    // Place a tombstone, it must be distinct from a nil key with a nil value.
    set_nil(&entry->key);
    set_boolean(&entry->value, false);
    return true;
}

void copy_table(VM *vm, Table *dst, const Table *src) {
    for (int i = 0; i < src->cap; i++) {
        const Entry *entry = &src->entries[i];
        if (is_nil(&entry->key)) {
            continue;
        }
        set_table(vm, dst, &entry->key, &entry->value);
    }
}

TString *find_interned(VM *vm, const char *data, int len, uint32_t hash) {
    Table *table = &vm->strings;
    if (table->count == 0) {
        return NULL;
    }
    uint32_t index = hash % table->cap;
    for (;;) {
        Entry *entry = &table->entries[index];
        if (is_nil(&entry->key)) {
            // Stop if we find a non-tombstone entry.
            if (is_nil(&entry->value)) {
                return NULL;
            }
        }
        // We assume ALL keys in this table are strings.
        TString *ts = as_string(&entry->key);
        if (ts->len == len && ts->hash == hash) {
            if (cstr_equal(ts->data, data, len)) {
                return ts;
            }
        }
        index = (index + 1) % table->cap;
    }
}

// 1}}} ------------------------------------------------------------------------
