/**
 * @brief   Host/user-facing API. Should not expose many internal functions,
 *          macros, datatypes and such in order not to pollute the global
 *          namespace.
 */
#ifndef LULU_H
#define LULU_H

#include "conf.h"

typedef struct VM        VM;
typedef NUMBER_TYPE      Number;
typedef struct Object    Object;
typedef struct Value     Value;
typedef struct ArrayList ArrayList; // A 1D array of type `Value`.
typedef struct HashMap   HashMap;
typedef struct String    String;
typedef struct Entry     Entry;
typedef struct Table     Table;

#endif /* LULU_H */
