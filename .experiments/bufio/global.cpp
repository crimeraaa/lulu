#include "global.hpp"

using Type = Value::Type;
using Pair = Table::Pair;
using Hash = String::Hash;

Value nil_value()
{
    Value v;
    get_type(&v)  = Type::Nil;
    as_number(&v) = 0;
    return v;
}

Value boolean_value(bool b)
{
    Value v;
    get_type(&v)   = Type::Boolean;
    as_boolean(&v) = b;
    return v;
}

Value number_value(Number n)
{
    Value v;
    get_type(&v) = Type::Number;
    as_number(&v) = n;
    return v;
}

Value string_value(String *s)
{
    Value v;
    get_type(&v)  = Type::String;
    as_object(&v) = cast_object(s);
    return v;
}

Value table_value(Table *t)
{
    Value v;
    get_type(&v)  = Type::Table;
    as_object(&v) = cast_object(t);
    return v;
}

const char *nameof_value(const Value *v)
{
    switch (get_type(v)) {
    case Type::Nil:     return "nil";
    case Type::Boolean: return "boolean";
    case Type::Number:  return "number";
    case Type::String:  return "string";
    case Type::Table:   return "table";
    }
}

const char *to_cstring_value(const Value *v, char *buf)
{
    int len = 0;
    switch (get_type(v)) {
    case Type::Nil: 
        return "nil";
    case Type::Boolean: 
        return as_boolean(v) ? "true" : "false";
    case Type::Number:
        len = sprintf(buf, "%.14g", as_number(v));
        break;
    case Type::String:
        return as_string(v)->data;
    default:
        len = sprintf(buf, "%s: %p", nameof_value(v), as_pointer(v));
        break;
    }
    buf[len] = '\0';
    return buf;
}

bool equal_values(const Value *a, const Value *b)
{
    if (get_type(a) != get_type(b))
        return false;

    switch (get_type(a)) {
    case Type::Nil:     return true;
    case Type::Boolean: return as_boolean(a) == as_boolean(b);
    case Type::Number:  return as_number(a) == as_number(b);
    case Type::String:  return as_string(a) == as_string(b);
    case Type::Table:   return as_table(a) == as_table(b);
    }
}

static void *stdc_allocate(void *ptr, size_t oldsz, size_t newsz, void *ctx)
{
    unused2(oldsz, ctx);
    if (newsz == 0) {
        free(ptr);
        return nullptr;
    }
    return std::realloc(ptr, newsz);
}

void free_object(Global *g, Object *o)
{
    switch (o->base.type) {
    case Type::String:
        free_string(g, cast_string(o));
        break;
    case Type::Table:
        free_table(g, cast_table(o));
        break;
    default:
        throw std::logic_error("Attempt to free a non-object");
    }
}

template<class T>
T *new_object(Global *g, Type t, size_t extra)
{
    Object *o = new_pointer<Object>(g, sizeof(T) + extra);
    o->base.next = g->objects; // chain main to instance
    o->base.type = t;
    g->objects   = o; // rearrange linked list
    return reinterpret_cast<T*>(o);
}

// --- STRING ------------------------------------------------------------- {{{1

static String::Hash hash_string(const char *cs, size_t len)
{
    static constexpr Hash FNV1A_PRIME64  = 0x00000100000001B3;
    static constexpr Hash FNV1A_OFFSET64 = 0xcbf29ce484222325;

    Hash hash = FNV1A_OFFSET64;
    for (size_t i = 0; i < len; i++) {
        hash ^= cs[i];
        hash *= FNV1A_PRIME64;
    }
    return hash;
}

String *new_string(Global *g, const char *cs, size_t len, String::Hash hash)
{
    String *s   = new_object<String>(g, Type::String);
    s->data     = new_array<char>(g, len + 1);
    s->length   = len;
    s->hash     = hash;
    std::memcpy(s->data, cs, len);
    s->data[len] = '\0';
    return s;
}

void free_string(Global *g, String *s)
{
    free_array(g, s->data, s->length + 1);
    free_pointer(g, s);
}

String *copy_string(Global *g, const char *cs, size_t len)
{
    Hash    hash  = hash_string(cs, len);
    String *found = lookup_string(g, cs, len, hash);
    if (found)
        return found;
    
    String *s = new_string(g, cs, len, hash);
    intern_string(g, s);
    return s;
}

void intern_string(Global *g, String *s)
{
    Table *t = g->strings;
    Value  k = string_value(s);
    set_table(g, t, &k, &k);
}

String *lookup_string(Global *g, const char *cs, size_t len, String::Hash hash)
{
    Table *t = g->strings;
    if (t->count == 0)
        return nullptr;
    
    Hash i = hash % t->capacity;
    for (;;) {
        Pair *p = &t->pairs[i];
        // Strings table may only have nil keys with nil values, or...
        if (is_nil(&p->key))
            return nullptr;
        // String keys mapped to themselves as values.
        String *s = as_string(&p->key);
        if (s->length == len && s->hash == hash) {
            if (memcmp(s->data, cs, len) == 0)
                return s;
        }
        i = (i + 1) % t->capacity;
    }
}

// --- 1}}} --------------------------------------------------------------------

// --- TABLE -------------------------------------------------------------- {{{1

static void clear_pairs(Pair *pairs, size_t cap)
{
    for (size_t i = 0; i < cap; i++) {
        Pair *p = &pairs[i];
        p->key = nil_value();
        p->val = nil_value();
    }    
}

Table *new_table(Global *g, size_t n)
{
    Table *t    = new_object<Table>(g, Type::Table);
    t->pairs    = (n > 0) ? new_array<Pair>(g, n) : nullptr;
    t->count    = 0;
    t->capacity = n;
    clear_pairs(t->pairs, t->capacity);
    return t;
}

void free_table(Global *g, Table *t)
{
    free_array(g, t->pairs, t->capacity);
    free_pointer(g, t);
}

template<class T>
static Hash hash_any(T v)
{
    char s[sizeof(v)];
    memcpy(s, &v, sizeof(s));
    return hash_string(s, sizeof(s));
}

static Hash get_hash(const Value *k)
{
    switch (k->type) {
    case Type::Nil: // Should not happen!
    case Type::Boolean: return as_boolean(k) ? 1 : 0;
    case Type::Number:  return hash_any(as_number(k));
    case Type::String:  return as_string(k)->hash;
    case Type::Table:   return hash_any(as_object(k));
    }
}

static Pair *find_pair(Pair *pairs, size_t cap, const Value *k)
{
    Hash  i    = get_hash(k) % cap;
    Pair *tomb = nullptr;

    for (;;) {
        Pair *pair = &pairs[i];
        if (is_nil(&pair->key)) {
            // Free slot?
            if (is_nil(&pair->val))
                return (tomb == nullptr) ? pair : tomb;
            // Nil key with non-nil value indicates it was unset, it is now free.
            if (tomb == nullptr)
                tomb = pair;
        } else if (equal_values(&pair->key, k)) {
            return pair;
        }
        i = (i + 1) % cap;
    }
}

Value *get_table(Table *t, const Value *k)
{
    if (t->count == 0 || is_nil(k))
        return nullptr;
    Pair *p = find_pair(t->pairs, t->capacity, k);
    return is_nil(&p->key) ? nullptr : &p->val;
}

static void resize_table(Global *g, Table *t, size_t next_cap)
{
    Pair *next_buf = new_array<Pair>(g, next_cap);
    clear_pairs(next_buf, next_cap);
    
    t->count = 0;
    for (size_t i = 0; i < t->capacity; i++) {
        Pair *src = &t->pairs[i];

        // Ignore tombstones and nil keys.
        if (is_nil(&src->key))
            continue;
        
        Pair *dst = find_pair(next_buf, next_cap, &src->key);
        dst->key = src->key;
        dst->val = src->val;
        t->count += 1;
    }
    free_array(g, t->pairs, t->capacity);
    t->pairs    = next_buf;
    t->capacity = next_cap;
}

Value *set_table(Global *g, Table *t, const Value *k, const Value *v)
{
    if (is_nil(k))
        return nullptr;
    if (t->count + 1 > t->capacity * TABLE_MAX_LOAD)
        resize_table(g, t, grow_capacity(t->capacity));
    
    Pair *p = find_pair(t->pairs, t->capacity, k);
    
    // Only increase count when we encounter non-tombstone nil keys.
    if (is_nil(&p->key) && is_nil(&p->val))
        t->count += 1;
    p->key = *k;
    p->val = *v;
    return &p->val;
}

void dump_table(Table *t)
{
    char buf[MAX_TO_CSTRING];
    for (size_t i = 0; i < t->capacity; i++) {
        Pair *p = &t->pairs[i];
        if (is_nil(&p->key))
            continue;
        // Separate calls so `buf` because `buf` is only 1 block of memory.
        printf("key: %s, ", to_cstring_value(&p->key, buf));
        printf("val: %s\n", to_cstring_value(&p->val, buf));
    }
}

// --- 1}}} --------------------------------------------------------------------

void init_global(Global *g)
{
    init_allocator(&g->allocator, &stdc_allocate, NULL);
    g->objects = NULL;

    // Now we can add to the linked list.
    g->strings = new_table(g, 0);
}

void free_global(Global *g)
{
    Object *obj = g->objects;

    dump_table(g->strings);
    while (obj != nullptr) {
        Object *next = obj->base.next;
        free_object(g, obj);
        obj = next;
    }
}
