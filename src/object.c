#include "object.h"
#include "limits.h"
#include "memory.h"
#include "vm.h"

static Object *_allocate_object(VM *vm, size_t size, VType tag) {
    Object *object = reallocate(NULL, 0, size);
    object->tag = tag;
    object->next = vm->objects; // Prepend: new list head
    vm->objects = object;
    return object;
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
    switch (get_tagtype(lhs)) {
    case TYPE_NIL:      return true;
    case TYPE_BOOLEAN:  return as_boolean(lhs) == as_boolean(rhs);
    case TYPE_NUMBER:   return num_eq(as_number(lhs), as_number(rhs));
    case TYPE_STRING: {
        const TString *ts1 = as_string(lhs);
        const TString *ts2 = as_string(rhs);
        if (ts1->len != ts2->len) {
            return false;
        }
        return cstr_equal(ts1->data, ts2->data, ts1->len);
    } break;
    default:
        // Should not happen
        return false;
    }
}

void init_tarray(TArray *self) {
    self->values = NULL;
    self->len = 0;
    self->cap = 0;
}

void free_tarray(TArray *self) {
    free_array(TValue, self->values, self->len);
    init_tarray(self);
}

void write_tarray(TArray *self, const TValue *value) {
    if (self->len + 1 > self->cap) {
        int oldcap = self->cap;
        self->cap = grow_capacity(oldcap);
        self->values = grow_array(TValue, self->values, oldcap, self->cap);
    }
    self->values[self->len] = *value;
    self->len++;
}

// TSTRING MANAGEMENT ----------------------------------------------------- {{{1

static TString *allocate_string(VM *vm, int len) {
    unused(vm);
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

static void build_string(TString *self, const char *source, int len) {
    char *end   = self->data; // For loop counter may skip.
    int skips   = 0;          // Number escape characters emitted.
    char prev   = 0;
    bool is_esc = false;
    for (int i = 0; i < len; i++) {
        char ch = source[i];
        // Handle `"\\"` appropriately.
        if (ch == '\\' && prev != '\\') {
            skips++;
            is_esc = true;
            prev   = ch;
            continue;
        }
        // TODO: 3-digit number literals (0-prefixed), 2-digit ASCII codes
        if (is_esc) {
            *end = get_escape(ch);
            is_esc = false;
            prev   = 0;
        } else {
            *end = ch;
        }
        end++;
    }
    self->len = len - skips;
    *end = '\0';
}

TString *copy_string(VM *vm, const char *literal, int len) {
    TString *inst = allocate_string(vm, len);
    build_string(inst, literal, len);
    return inst;
}

TString *concat_strings(VM *vm, const TString *lhs, const TString *rhs) {
    int len = lhs->len + rhs->len;
    TString *inst = allocate_string(vm, len);

    // Don't use `build_string` as it would have already been called for both.
    memcpy(inst->data,            lhs->data, lhs->len);
    memcpy(inst->data + lhs->len, rhs->data, rhs->len);
    inst->data[len] = '\0';
    return inst;
}

// 1}}} ------------------------------------------------------------------------
