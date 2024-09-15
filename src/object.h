#ifndef LULU_OBJECT_H
#define LULU_OBJECT_H

#include "value.h"

struct lulu_Object {
    lulu_Value_Type type;
    lulu_Object    *next;
};

lulu_Object *
lulu_Object_new(lulu_VM *vm, lulu_Value_Type type, isize size);

void
lulu_Object_free(lulu_VM *vm, lulu_Object *self);

#endif // LULU_OBJECT_H
