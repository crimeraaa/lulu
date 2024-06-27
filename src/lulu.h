/**
 * @brief   Host/user-facing API. Should not expose many internal functions,
 *          macros, datatypes and such in order not to pollute the global
 *          namespace.
 */
#ifndef LULU_H
#define LULU_H

#include "conf.h"

typedef LULU_NUMBER_TYPE   lulu_Number;
typedef struct lulu_VM     lulu_VM;     // defined in `vm.h`.
typedef struct lulu_Value  lulu_Value;  // defined in `object.h`.
typedef struct lulu_String lulu_String; // defined in `object.h`.
typedef struct lulu_Table  lulu_Table;  // defined in `object.h`.
typedef void *(*lulu_Allocator)(void *ptr, size_t oldsz, size_t newsz, void *ctx);
typedef const char *(*lulu_Reader)(lulu_VM *vm, size_t sz, void *ctx);

typedef enum {
    LULU_OK,
    LULU_ERROR_COMPTIME,
    LULU_ERROR_RUNTIME,
    LULU_ERROR_ALLOC,
} lulu_Status;

#endif /* LULU_H */
