#ifndef LULU_OBJECT_H
#define LULU_OBJECT_H

#include "lulu.h"
#include "value.h"
#include "string.h"

struct lulu_Object {
    lulu_Value_Type type;
    lulu_Object    *next;
};

struct lulu_String {
    lulu_Object base;
    isize       len;
    char        data[];
};

#define lulu_Value_cast_string(self)    (cast(lulu_String *)(self)->object)

lulu_Object *
lulu_Object_new(lulu_VM *vm, lulu_Value_Type type, isize size);

void
lulu_Object_free(lulu_VM *vm, lulu_Object *self);

lulu_String *
lulu_String_new(lulu_VM *vm, lulu_String_View string);

void
lulu_String_free(lulu_VM *vm, lulu_String *self);

#endif // LULU_OBJECT_H
