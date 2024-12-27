#ifndef LULU_OBJECT_H
#define LULU_OBJECT_H

#include "value.h"

struct Object {
    Value_Type type;
    Object    *next;
};

Object *
object_new(lulu_VM *vm, Value_Type type, isize size);

void
object_free(lulu_VM *vm, Object *self);

#endif // LULU_OBJECT_H
