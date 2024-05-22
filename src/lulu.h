/**
 * @brief   Host/user-facing API. Should not expose many internal functions,
 *          macros, datatypes and such in order not to pollute the global
 *          namespace.
 */
#ifndef LULU_H
#define LULU_H

#include "conf.h"

typedef NUMBER_TYPE lulu_Number;

struct lulu_VM;     // defined in `vm.h`.
struct lulu_Value;  // defined in `object.h`.
struct lulu_String; // defined in `object.h`.
struct lulu_Table;  // defined in `object.h`.

#endif /* LULU_H */
