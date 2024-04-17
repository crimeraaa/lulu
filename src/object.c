#include "object.h"
#include "limits.h"
#include "memory.h"
#include "vm.h"

// MEMORY MANAGEMENT ------------------------------------------------------ {{{1

// Returns `node` since assignment expressions evaluate to the rvalue.
#define prepend_node(head, node)    ((node)->next = (head), (head) = (node))
#define remove_node(head, node)     ((head) = (node)->next)

// Separate name from `new_object` since `new_tstring` also needs access.
static Object *_new_object(size_t size, VType tag, Allocator *allocator) {
    VM *vm = cast(VM*, allocator->context);
    Object *object = allocator->allocfn(NULL, 0, size, vm);
    object->tag    = tag;
    return prepend_node(vm->objects, object);
}

#define new_object(T, tag, allocator) \
    cast(T*, _new_object(sizeof(T), tag, allocator))

// Not meant to be used outside of the main `new_tstring()` function.
#define _new_tstring(N, allocator) \
    cast(TString*, _new_object(tstring_size(N), TYPE_STRING, allocator))

// NOTE: For `concat_string` we do not know the correct hash yet.
// Analogous to `allocateString()` in the book.
static TString *new_tstring(int len, Allocator *allocator) {
    TString *inst = _new_tstring(len + 1, allocator);
    inst->len     = len;
    return inst;
}

// 1}}} ------------------------------------------------------------------------

const char *const LULU_TYPENAMES[] = {
    [TYPE_NIL]     = "nil",
    [TYPE_BOOLEAN] = "boolean",
    [TYPE_NUMBER]  = "number",
    [TYPE_STRING]  = "string",
};

void print_value(const TValue *self) {
    switch (get_tag(self)) {
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
    if (get_tag(lhs) != get_tag(rhs)) {
        return false;
    }
    // For objects we assume they are correctly interned.
    switch (get_tag(lhs)) {
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

void free_tarray(TArray *self, Allocator *allocator) {
    free_array(TValue, self->values, self->len, allocator);
    init_tarray(self);
}

void write_tarray(TArray *self, const TValue *value, Allocator *allocator) {
    if (self->len + 1 > self->cap) {
        int oldcap   = self->cap;
        int newcap   = grow_capacity(oldcap);
        TValue *ptr  = self->values;
        self->values = resize_array(TValue, ptr, oldcap, newcap, allocator);
        self->cap    = newcap;
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

// Hashes strings with unencoded escape sequences correctly.
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
    char *end = self->data; // For loop counter may skip.
    int skips = 0;          // Number escape characters emitted.
    char prev = 0;
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
    Allocator *allocator = &vm->allocator;
    uint32_t hash        = hash_string(literal, len);
    TString *interned    = find_interned(vm, literal, len, hash);

    // Is this string already interned?
    if (interned != NULL) {
        return interned;
    }

    TString *inst = new_tstring(len, allocator);
    build_string(inst, literal, len, hash);

    // If we have escapes, are we really REALLY sure this isn't interned?
    if (inst->len != len) {
        interned = find_interned(vm, inst->data, inst->len, inst->hash);
        if (interned != NULL) {
            remove_node(vm->objects, &inst->object);
            free_tstring(inst, inst->len, allocator);
            return interned;
        }
    }
    set_interned(vm, inst);
    return inst;
}

TString *concat_strings(VM *vm, int argc, const TValue argv[], int len) {
    Allocator *allocator = &vm->allocator;
    TString *inst = new_tstring(len, allocator);
    int offset = 0;

    // We already built each individual string so no need to interpret escapes.
    for (int i = 0; i < argc; i++) {
        const TString *arg = as_string(&argv[i]);
        memcpy(inst->data + offset, arg->data, arg->len);
        offset += arg->len;
    }
    inst->hash = hash_string(inst->data, inst->len);
    inst->data[inst->len] = '\0';

    TString *interned = find_interned(vm, inst->data, inst->len, inst->hash);
    if (interned != NULL) {
        remove_node(vm->objects, &inst->object);
        free_tstring(inst, inst->len, allocator);
        return interned;
    }
    set_interned(vm, inst);
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

void free_table(Table *self, Allocator *allocator) {
    free_array(Entry, self->entries, self->cap, allocator);
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
    switch (get_tag(self)) {
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
static void resize_table(Table *self, int newcap, Allocator *allocator) {
    Entry *newbuf = new_array(Entry, newcap, allocator);
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
    free_array(Entry, self->entries, self->cap, allocator);
    self->entries = newbuf;
    self->cap     = newcap;
}

bool get_table(Table *self, const TValue *key, TValue *out) {
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

bool set_table(Table *self, const TValue *key, const TValue *value, Allocator *allocator) {
    if (self->count + 1 > self->cap * TABLE_MAX_LOAD) {
        resize_table(self, grow_capacity(self->cap), allocator);
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

bool unset_table(Table *self, const TValue *key) {
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

void copy_table(Table *dst, const Table *src, Allocator *allocator) {
    for (int i = 0; i < src->cap; i++) {
        const Entry *entry = &src->entries[i];
        if (is_nil(&entry->key)) {
            continue;
        }
        set_table(dst, &entry->key, &entry->value, allocator);
    }
}

void set_interned(VM *vm, const TString *string) {
    Allocator *allocator = &vm->allocator;
    const TValue key = make_string(string);
    const TValue val = make_boolean(true);
    set_table(&vm->strings, &key, &val, allocator);
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
