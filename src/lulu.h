/**
 * @brief   Host/user-facing API. Should not expose many internal functions,
 *          macros, datatypes and such in order not to pollute the global
 *          namespace.
 */
#ifndef LULU_H
#define LULU_H

#include "conf.h"

typedef NUMBER_TYPE lulu_Number;

typedef struct lulu_VM     lulu_VM;     // defined in `vm.h`.
typedef struct lulu_Value  lulu_Value;  // defined in `object.h`.
typedef struct lulu_String lulu_String; // defined in `object.h`.
typedef struct lulu_Table  lulu_Table;  // defined in `object.h`.

typedef enum {
    LULU_ERROR_NONE,
    LULU_ERROR_COMPTIME,
    LULU_ERROR_RUNTIME,
    LULU_ERROR_ALLOC,
} lulu_ErrorCode;

#endif /* LULU_H */
