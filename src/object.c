#include "object.h"
#include "limits.h"
#include "memory.h"
#include "string.h"
#include "table.h"
#include "vm.h"
#include "api.h"

const char *const LULU_TYPENAMES[] = {
    [TYPE_NIL]     = "nil",
    [TYPE_BOOLEAN] = "boolean",
    [TYPE_NUMBER]  = "number",
    [TYPE_STRING]  = "string",
    [TYPE_TABLE]   = "table",
};

const Value *value_tonumber(Value *self)
{
    if (is_number(self)) {
        return self;
    }
    if (is_string(self)) {
        String *s    = as_string(self);
        StrView view = make_strview(s->data, s->len);
        char   *end;
        Number  n    = cstr_tonumber(view.begin, &end);
        if (end != view.end) {
            return NULL;
        }
        setv_number(self, n);
        return self;
    }
    return NULL;
}

const char *value_tocstring(const Value *self, char *buffer, int *out)
{
    int len = 0;
    if (out != NULL) {
        *out = -1;
    }
    switch (get_tag(self)) {
    case TYPE_NIL:
        return "nil";
    case TYPE_BOOLEAN:
        return as_boolean(self) ? "true" : "false";
    case TYPE_NUMBER:
        len = num_tostring(buffer, as_number(self));
        break;
    case TYPE_STRING:
        return as_cstring(self);
    case TYPE_TABLE:
        len = snprintf(buffer,
                       MAX_TOSTRING,
                       "%s: %p",
                       get_typename(self),
                       as_pointer(self));
        break;
    }
    buffer[len] = '\0';
    if (out != NULL) {
        *out = len;
    }
    return buffer;
}

void print_value(const Value *self, bool isdebug)
{
    if (is_string(self) && isdebug) {
        const String *s = as_string(self);
        // printf("string: %p ", as_pointer(self));
        if (s->len <= 1) {
            printf("\'%s\'", s->data);
        } else {
            printf("\"%s\"", s->data);
        }
        // printf(" (len: %i, hash: %u)", s->len, s->object.hash);
    } else {
        char buffer[MAX_TOSTRING];
        printf("%s", value_tocstring(self, buffer, NULL));
    }
}

bool values_equal(const Value *lhs, const Value *rhs)
{
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

void init_varray(VArray *self)
{
    self->values = NULL;
    self->len    = 0;
    self->cap    = 0;
}

void free_varray(VArray *self, Alloc *alloc)
{
    // free_array(Value, self->values, self->len, alloc);
    free_parray(self->values, self->len, alloc);
    init_varray(self);
}

void write_varray(VArray *self, const Value *value, Alloc *alloc)
{
    if (self->len + 1 > self->cap) {
        int oldcap   = self->cap;
        int newcap   = grow_capacity(oldcap);
        // self->values = resize_array(Value, self->values, oldcap, newcap, alloc);
        self->values = resize_parray(self->values, oldcap, newcap, alloc);
        self->cap    = newcap;
    }
    self->values[self->len] = *value;
    self->len += 1;
}

void set_interned(VM *vm, const String *string)
{
    Alloc *alloc = &vm->alloc;
    Value  key   = make_string(string);
    Value  val   = make_boolean(true);
    set_table(&vm->strings, &key, &val, alloc);
}

String *find_interned(VM *vm, StrView view, uint32_t hash)
{
    Table *table = &vm->strings;
    if (table->count == 0) {
        return NULL;
    }
    uint32_t index = hash % table->cap;
    for (;;) {
        Entry *entry = &table->entries[index];
        // The strings table only ever has completely empty or full entries.
        if (is_nil(&entry->key) && is_nil(&entry->value)) {
            return NULL;
        }
        // We assume ALL valid (i.e: non-nil) keys are strings.
        String *interned = as_string(&entry->key);
        if (interned->len == view.len && interned->hash == hash) {
            if (cstr_eq(interned->data, view.begin, view.len)) {
                return interned;
            }
        }
        index = (index + 1) % table->cap;
    }
}
