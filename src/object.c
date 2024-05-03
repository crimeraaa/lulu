#include "object.h"
#include "limits.h"
#include "memory.h"
#include "vm.h"

// MEMORY MANAGEMENT ------------------------------------------------------ {{{1

// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
#define FNV1A_PRIME32   0x01000193
#define FNV1A_OFFSET32  0x811c9dc5
#define FNV1A_PRIME64   0x00000100000001B3
#define FNV1A_OFFSET64  0xcbf29ce484222325

static char get_escape(char ch) {
    switch (ch) {
    case '\\':  return '\\';
    case '\'':  return '\'';
    case '\"':  return '\"';

    case '0':   return '\0';
    case 'a':   return '\a';
    case 'b':   return '\b';
    case 'f':   return '\f';
    case 'n':   return '\n';
    case 'r':   return '\r';
    case 't':   return '\t';
    case 'v':   return '\v';

    default:    return ch;   // TODO: Warn user? Throw error?
    }
}

// Note that we need to hash escapes correctly too.
static uint32_t hash_string(const StrView *view) {
    uint32_t hash = FNV1A_OFFSET32;
    char     prev = 0;
    for (const char *ptr = view->begin; ptr < view->end; ptr++) {
        char ch = *ptr;
        if (ch == '\\' && prev != '\\') {
            prev = ch;
            continue;
        }
        hash ^= cast(Byte, (prev == '\\') ? get_escape(ch) : ch);
        hash *= FNV1A_PRIME32;
        if (prev == '\\') {
            prev = 0;
        }
    }
    return hash;
}

// This function will not hash any escape sequences at all.
static uint32_t hash_lstring(const StrView *view) {
    uint32_t hash = FNV1A_OFFSET32;
    for (const char *ptr = view->begin; ptr < view->end; ptr++) {
        hash ^= cast(Byte, *ptr);
        hash *= FNV1A_PRIME32;
    }
    return hash;
}

static uint32_t hash_pointer(Object *ptr) {
    union {
        Object *data;
        char    raw[sizeof(ptr)];
    } hash;
    hash.data    = ptr;
    StrView view = make_strview(hash.raw, sizeof(hash.raw));
    return hash_lstring(&view);
}

static uint32_t hash_number(Number number) {
    union {
        Number data;
        char   raw[sizeof(number)];
    } hash;
    hash.data    = number;
    StrView view = make_strview(hash.raw, sizeof(hash.raw));
    return hash_lstring(&view);
}

// Separate name from `new_object` since `new_tstring` also needs access.
// Assumes casting `alloc->context` to `VM*` is a safe operation.
static Object *_new_object(size_t size, VType tag, Alloc *alloc) {
    VM     *vm   = alloc->context;
    Object *node = alloc->reallocfn(NULL, 0, size, vm);
    node->tag    = tag;
    node->hash   = hash_pointer(node);
    return prepend_object(&vm->objects, node);
}

#define new_object(T, tag, alloc) \
    cast(T*, _new_object(sizeof(T), tag, alloc))

// Not meant to be used outside of the main `new_tstring()` function.
// Note how we add 1 for the nul char.
#define _new_tstring(N, alloc) \
    cast(TString*, _new_object(tstring_size(N) + 1, TYPE_STRING, alloc))

// 1}}} ------------------------------------------------------------------------

const char *const LULU_TYPENAMES[] = {
    [TYPE_NIL]     = "nil",
    [TYPE_BOOLEAN] = "boolean",
    [TYPE_NUMBER]  = "number",
    [TYPE_STRING]  = "string",
    [TYPE_TABLE]   = "table",
};

const char *to_cstring(const TValue *self, char *buffer, int *len) {
    int n = 0;
    if (len != NULL) {
        *len = -1;
    }
    switch (get_tag(self)) {
    case TYPE_NIL:
        return "nil";
    case TYPE_BOOLEAN:
        return as_boolean(self) ? "true" : "false";
    case TYPE_NUMBER:
        n = num_tostring(buffer, as_number(self));
        break;
    case TYPE_STRING:
        return as_cstring(self);
    case TYPE_TABLE:
        n = snprintf(buffer,
                     MAX_TOSTRING,
                     "%s: %p",
                     get_typename(self),
                     as_pointer(self));
        break;
    }
    buffer[n] = '\0';
    if (len != NULL) {
        *len = n;
    }
    return buffer;
}

void print_value(const TValue *self, bool quoted) {
    if (is_string(self) && quoted) {
        const TString *s = as_string(self);
        // printf("string: %p ", as_pointer(self));
        if (s->len <= 1) {
            printf("\'%s\'", s->data);
        } else {
            printf("\"%s\"", s->data);
        }
        // printf(" (len: %i, hash: %u)", s->len, s->object.hash);
    } else {
        char buffer[MAX_TOSTRING];
        printf("%s", to_cstring(self, buffer, NULL));
    }
}

bool values_equal(const TValue *lhs, const TValue *rhs) {
    // Logically, differing types can never be equal.
    if (get_tag(lhs) != get_tag(rhs)) {
        return false;
    }
    switch (get_tag(lhs)) {
    case TYPE_NIL:      return true;
    case TYPE_BOOLEAN:  return as_boolean(lhs) == as_boolean(rhs);
    case TYPE_NUMBER:   return num_eq(as_number(lhs), as_number(rhs));
    case TYPE_STRING:   // We assume all objects are correctly interned.
    case TYPE_TABLE:    return as_object(lhs) == as_object(rhs);
    }
}

void init_tarray(TArray *self) {
    self->values = NULL;
    self->len = 0;
    self->cap = 0;
}

void free_tarray(TArray *self, Alloc *alloc) {
    free_array(TValue, self->values, self->len, alloc);
    init_tarray(self);
}

void write_tarray(TArray *self, const TValue *value, Alloc *alloc) {
    if (self->len + 1 > self->cap) {
        int oldcap   = self->cap;
        int newcap   = grow_capacity(oldcap);
        TValue *ptr  = self->values;
        self->values = resize_array(TValue, ptr, oldcap, newcap, alloc);
        self->cap    = newcap;
    }
    self->values[self->len] = *value;
    self->len += 1;
}

// TSTRING MANAGEMENT ----------------------------------------------------- {{{1

// NOTE: For `concat_string` we do not know the correct hash yet.
// Analogous to `allocateString()` in the book.
static TString *new_tstring(int len, Alloc *alloc) {
    TString *inst = _new_tstring(len, alloc);
    inst->len     = len;
    return inst;
}

static void build_string(TString *self, const StrView *view) {
    char   *end   = self->data; // For loop counter may skip.
    int     skips = 0;          // Number escape characters emitted.
    char    prev  = 0;

    for (const char *ptr = view->begin; ptr < view->end; ptr++) {
        char ch = *ptr;
        // Handle `"\\"` appropriately.
        if (ch == '\\' && prev != '\\') {
            skips++;
            prev = ch;
            continue;
        }
        // TODO: 3-digit number literals (0-prefixed), 2-digit ASCII codes
        if (prev == '\\') {
            *end = get_escape(ch);
            prev = 0;
        } else {
            *end = ch;
        }
        end++;
    }
    *end      = '\0';
    self->len = view->len - skips;
}

static void end_string(TString *self, uint32_t hash) {
    self->data[self->len] = '\0';
    self->object.hash     = hash;
}

static TString *copy_string_or_lstring(VM *vm, const StrView *view, bool islong) {
    Alloc    *alloc    = &vm->alloc;
    uint32_t  hash     = (islong) ? hash_lstring(view) : hash_string(view);
    TString  *interned = find_interned(vm, view, hash);

    // Is this string already interned?
    if (interned != NULL) {
        return interned;
    }

    TString *inst = new_tstring(view->len, alloc);
    if (islong) {
        memcpy(inst->data, view->begin, view->len);
    } else {
        build_string(inst, view);
    }
    end_string(inst, hash);

    // If we have escapes, are we really REALLY sure this isn't interned?
    if (inst->len != view->len) {
        StrView v2 = make_strview(inst->data, inst->len);
        interned   = find_interned(vm, &v2, hash);
        if (interned != NULL) {
            remove_object(&vm->objects, &inst->object);
            free_tstring(inst, inst->len, alloc);
            return interned;
        }
    }
    set_interned(vm, inst);
    return inst;
}

TString *copy_lstring(VM *vm, const StrView *view) {
    return copy_string_or_lstring(vm, view, true);
}

TString *copy_string(VM *vm, const StrView *view) {
    return copy_string_or_lstring(vm, view, false);
}

TString *concat_strings(VM *vm, int argc, const TValue argv[], int len) {
    Alloc   *alloc  = &vm->alloc;
    TString *inst   = new_tstring(len, alloc);
    StrView  view   = make_strview(inst->data, inst->len);
    int      offset = 0;

    // We already built each individual string so no need to interpret escapes.
    for (int i = 0; i < argc; i++) {
        const TString *arg = as_string(&argv[i]);
        memcpy(inst->data + offset, arg->data, arg->len);
        offset += arg->len;
    }
    end_string(inst, hash_string(&view));
    TString *interned = find_interned(vm, &view, inst->object.hash);
    if (interned != NULL) {
        remove_object(&vm->objects, &inst->object);
        free_tstring(inst, inst->len, alloc);
        return interned;
    }
    set_interned(vm, inst);
    return inst;
}

// 1}}} ------------------------------------------------------------------------

// TABLE MANAGEMENT ------------------------------------------------------- {{{1

#define TABLE_MAX_LOAD  0.75

Table *new_table(Alloc *alloc) {
    Table *inst = new_object(Table, TYPE_TABLE, alloc);
    init_table(inst);
    return inst;
}

void init_table(Table *self) {
    self->entries = NULL;
    self->count   = 0;
    self->cap     = 0;
}

void free_table(Table *self, Alloc *alloc) {
    free_array(Entry, self->entries, self->cap, alloc);
    init_table(self);
}

void dump_table(const Table *self, const char *name) {
    name = (name != NULL) ? name : "(anonymous table)";
    if (self->count == 0) {
        printf("%s = {}\n", name);
        return;
    }
    printf("%s = {\n", name);
    for (int i = 0, limit = self->cap; i < limit; i++) {
        const Entry *entry = &self->entries[i];
        if (is_nil(&entry->key)) {
            continue;
        }
        printf("\t[");
        print_value(&entry->key, true);
        printf("] = ");
        print_value(&entry->value, true);
        printf(",\n");
    }
    printf("}\n");
}

static uint32_t get_hash(const TValue *self) {
    switch (get_tag(self)) {
    case TYPE_NIL:      return 0; // WARNING: We should never hash `nil`!
    case TYPE_BOOLEAN:  return as_boolean(self);
    case TYPE_NUMBER:   return hash_number(as_number(self));
    case TYPE_STRING:   // All objects pre-determine their hash value already.
    case TYPE_TABLE:    return as_object(self)->hash;
    }
}

// Find a free slot. Assumes there is at least 1 free slot left.
static Entry *find_entry(Entry *list, int cap, const TValue *key) {
    uint32_t index = get_hash(key) % cap;
    Entry   *tombstone = NULL;
    for (;;) {
        Entry *entry = &list[index];
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
static void resize_table(Table *self, int newcap, Alloc *alloc) {
    Entry *newbuf = new_array(Entry, newcap, alloc);
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
    free_array(Entry, self->entries, self->cap, alloc);
    self->entries = newbuf;
    self->cap     = newcap;
}

bool get_table(Table *self, const TValue *key, TValue *out) {
    if (self->count == 0 || is_nil(key)) {
        return false;
    }
    Entry *entry = find_entry(self->entries, self->cap, key);
    if (is_nil(&entry->key)) {
        return false;
    }
    *out = entry->value;
    return true;
}

bool set_table(Table *self, const TValue *key, const TValue *value, Alloc *alloc) {
    if (is_nil(key)) {
        return false;
    }
    if (self->count + 1 > self->cap * TABLE_MAX_LOAD) {
        resize_table(self, grow_capacity(self->cap), alloc);
    }
    Entry *entry    = find_entry(self->entries, self->cap, key);
    bool   isnewkey = is_nil(&entry->key);

    // Don't increase the count for tombstones (nil-key with non-nil value)
    if (isnewkey && is_nil(&entry->value)) {
        self->count++;
    }
    entry->key   = *key;
    entry->value = *value;
    return isnewkey;
}

bool unset_table(Table *self, const TValue *key) {
    if (self->count == 0 || is_nil(key)) {
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

void copy_table(Table *dst, const Table *src, Alloc *alloc) {
    for (int i = 0; i < src->cap; i++) {
        const Entry *entry = &src->entries[i];
        if (is_nil(&entry->key)) {
            continue;
        }
        set_table(dst, &entry->key, &entry->value, alloc);
    }
}

void set_interned(VM *vm, const TString *string) {
    Alloc  *alloc = &vm->alloc;
    TValue  key   = make_string(string);
    TValue  val   = make_boolean(true);
    set_table(&vm->strings, &key, &val, alloc);
}

TString *find_interned(VM *vm, const StrView *view, uint32_t hash) {
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
        if (ts->len == view->len && ts->object.hash == hash) {
            if (cstr_eq(ts->data, view->begin, view->len)) {
                return ts;
            }
        }
        index = (index + 1) % table->cap;
    }
}

// 1}}} ------------------------------------------------------------------------
