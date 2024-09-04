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

/**
 * @param out   Will be set to the number of characters in the buffer.
 * @param ctx   Userdata/context pointer.
 */
typedef const char *(*lulu_Reader)(lulu_VM *vm, size_t *out, void *ctx);

typedef enum {
    LULU_OK,
    LULU_ERROR_COMPTIME,
    LULU_ERROR_RUNTIME,
    LULU_ERROR_ALLOC,
} lulu_Status;

lulu_VM *lulu_open(void);
void     lulu_close(lulu_VM *vm);
void     lulu_set_top(lulu_VM *vm, int offset);
#define  lulu_pop(vm, n) lulu_set_top((vm), -(n))

// Caller must allocate `input` properly however they need.
lulu_Status lulu_load(lulu_VM *vm, const char *input, size_t len, const char *name);
const char *lulu_get_typename(lulu_VM *vm, int offset);

bool lulu_is_nil(lulu_VM *vm, int offset);
bool lulu_is_number(lulu_VM *vm, int offset);
bool lulu_is_boolean(lulu_VM *vm, int offset);
bool lulu_is_string(lulu_VM *vm, int offset);
bool lulu_is_table(lulu_VM *vm, int offset);

void    lulu_push_nil(lulu_VM *vm, int count);
void    lulu_push_boolean(lulu_VM *vm, bool b);
void    lulu_push_number(lulu_VM *vm, lulu_Number n);
void    lulu_push_string(lulu_VM *vm, const char *s);
void    lulu_push_lstring(lulu_VM *vm, const char *s, size_t len);
void    lulu_push_table(lulu_VM *vm, lulu_Table *t);
#define lulu_push_literal(vm, s) lulu_push_lstring((vm), (s), lulu_cstr_len(s))

/**
 * @brief   Internal use for writing simple formatted messages.
 *
 * @details The following format specifiers are allowed:
 *          `%c` - pushes an `int` as a string holding 1 ASCII character.
 *          `%f` - pushes a `lulu_Number` in its string representation.
 *          `%i` - pushes an `int` in its string representation.
 *          `%s` - allocates/reuses a `String*` based on the `const char *`.
 *          `%p` - pushes platform-specific pointer representation.
 *
 * @note    No modifiers are allowed.
 *          See: https://www.lua.org/manual/5.1/manual.html#lua_pushfstring
 */
const char *lulu_push_vfstring(lulu_VM *vm, const char *fmt, va_list args);
const char *lulu_push_fstring(lulu_VM *vm, const char *fmt, ...);

// In-place conversions to some fundamental datatypes.
bool         lulu_to_boolean(lulu_VM *vm, int offset);
lulu_Number  lulu_to_number(lulu_VM *vm, int offset);
const char  *lulu_to_string(lulu_VM *vm, int offset);
const char  *lulu_concat(lulu_VM *vm, int count);

void lulu_get_table(lulu_VM *vm, int t_offset, int k_offset);
void lulu_set_table(lulu_VM *vm, int t_offset, int k_offset, int to_pop);
void lulu_get_global(lulu_VM *vm, const char *s);
void lulu_set_global(lulu_VM *vm, const char *s);

void lulu_comptime_error(lulu_VM *vm, int line, const char *what, const char *where);
void lulu_runtime_error(lulu_VM *vm, const char *fmt, ...);
void lulu_alloc_error(lulu_VM *vm);

void lulu_type_error(lulu_VM *vm, const char *act, const char *type);
void lulu_push_error_fstring(lulu_VM *vm, int line, const char *fmt, ...);
void lulu_push_error_vfstring(lulu_VM *vm, int line, const char *fmt, va_list args);

#endif /* LULU_H */
