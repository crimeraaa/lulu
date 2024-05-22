#ifndef LULU_API_H
#define LULU_API_H

#include "lulu.h"

#ifdef __GNUC__

// See: https://gcc.gnu.org/onlinedocs/gcc-3.2/gcc/Function-Attributes.html
#define lulu_check_format(fmt, arg) \
    __attribute__((__format__ (__printf__, fmt, arg)))

#else // __GNUC__ not defined.

// Attributes are a GCC extension.
#define lulu_check_format(fmt, arg)

#endif // __GNUC__

void    lulu_set_top(lulu_VM *vm, int offset);
#define lulu_pop(vm, n) lulu_set_top((vm), -(n))

void    lulu_push_nil(lulu_VM *vm, int count);
void    lulu_push_boolean(lulu_VM *vm, bool b);
void    lulu_push_number(lulu_VM *vm, lulu_Number n);
void    lulu_push_string(lulu_VM *vm, lulu_String *s);
void    lulu_push_cstring(lulu_VM *vm, const char *s);
void    lulu_push_lcstring(lulu_VM *vm, const char *s, int len);
void    lulu_push_table(lulu_VM *vm, lulu_Table *t);
#define lulu_push_literal(vm, s) lulu_push_lcstring((vm), (s), cstr_len(s))

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
const char *lulu_push_vfstring(lulu_VM *vm, const char *fmt, va_list ap)
            lulu_check_format(2, 0);
const char *lulu_push_fstring(lulu_VM *vm, const char *fmt, ...)
            lulu_check_format(2, 3);

// In-place conversions to some fundamental datatypes.
bool         lulu_to_boolean(lulu_VM *vm, int offset);
lulu_Number  lulu_to_number(lulu_VM *vm, int offset);
lulu_String *lulu_to_string(lulu_VM *vm, int offset);
const char  *lulu_to_cstring(lulu_VM *vm, int offset);
const char  *lulu_concat(lulu_VM *vm, int count);

void lulu_get_table(lulu_VM *vm, int t_offset, int k_offset);
void lulu_set_table(lulu_VM *vm, int t_offset, int k_offset, int to_pop);
void lulu_get_global(lulu_VM *vm, lulu_String *s);
void lulu_set_global(lulu_VM *vm, lulu_String *s);

void lulu_type_error(lulu_VM *vm, const char *act, const char *type);
void lulu_comptime_error(lulu_VM *vm, int line, const char *what, const char *where);
void lulu_runtime_error(lulu_VM *vm, const char *fmt, ...)
     lulu_check_format(2, 3);
void lulu_push_error_fstring(lulu_VM *vm, int line, const char *fmt, ...)
     lulu_check_format(3, 4);
void lulu_push_error_vfstring(lulu_VM *vm, int line, const char *fmt, va_list ap)
     lulu_check_format(3, 0);

#endif /* LULU_API_H */
