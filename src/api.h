#ifndef LULU_API_H
#define LULU_API_H

#include "lulu.h"

void lulu_push_nil(lulu_VM *vm, int count);
void lulu_push_boolean(lulu_VM *vm, bool b);
void lulu_push_number(lulu_VM *vm, lulu_Number n);
void lulu_push_string(lulu_VM *vm, lulu_String *s);
void lulu_push_cstring(lulu_VM *vm, const char *s);
void lulu_push_lcstring(lulu_VM *vm, const char *s, int len);
void lulu_push_table(lulu_VM *vm, lulu_Table *t);

#define lulu_push_literal(vm, s) lulu_push_lcstring((vm), (s), cstr_len(s))

/**
 * @brief   Internal use for writing simple formatted messages.
 *          Only accepts the following format specifiers: c, i, s and p.
 *
 * @note    No modifiers are allowed.
 */
const char *lulu_push_vfstring(lulu_VM *vm, const char *fmt, va_list ap);
const char *lulu_push_fstring(lulu_VM *vm, const char *fmt, ...);

/**
 * @brief   Converts the value at the given `offset` to a `String*`.
 *
 * @return  The `String::data` member thereof.
 *
 * @note    You may want to pop this value if it came from `lulu_*_error`.
 */
const char *lulu_tostring(lulu_VM *vm, int offset);
const char *lulu_concat(lulu_VM *vm, int count);

void lulu_get_table(lulu_VM *vm, int t_offset, int k_offset);
void lulu_set_table(lulu_VM *vm, int t_offset, int k_offset, int to_pop);
void lulu_get_global(lulu_VM *vm, const lulu_Value *k);
void lulu_set_global(lulu_VM *vm, const lulu_Value *k);

void lulu_type_error(lulu_VM *vm, const char *act, const char *type);

#endif /* LULU_API_H */
