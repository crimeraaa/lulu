#ifndef LULU_API_H
#define LULU_API_H

#include "lulu.h"

void lulu_push_nil(struct lulu_VM *vm, int count);
void lulu_push_boolean(struct lulu_VM *vm, bool b);
void lulu_push_number(struct lulu_VM *vm, lulu_Number n);
void lulu_push_string(struct lulu_VM *vm, struct lulu_String *s);
void lulu_push_cstring(struct lulu_VM *vm, const char *s);
void lulu_push_lcstring(struct lulu_VM *vm, const char *s, int len);
void lulu_push_table(struct lulu_VM *vm, struct lulu_Table *t);

/**
 * @brief   Internal use for writing simple formatted messages.
 *          Only accepts the following format specifiers: c, i, s and p.
 *
 * @note    No modifiers are allowed.
 */
const char *lulu_push_vfstring(struct lulu_VM *vm, const char *fmt, va_list argp);
const char *lulu_push_fstring(struct lulu_VM *vm, const char *fmt, ...);

/**
 * @brief   Pushes the C-string representation, wrapped in a `String*`, of the
 *          value at the given `offset`.
 *
 * @return  The `String::data` member thereof.
 */
const char *lulu_tostring(struct lulu_VM *vm, int offset);

void lulu_get_table(struct lulu_VM *vm, int t_offset, int k_offset);
void lulu_set_table(struct lulu_VM *vm, int t_offset, int k_offset, int to_pop);
void lulu_get_global(struct lulu_VM *vm, const struct lulu_Value *k);
void lulu_set_global(struct lulu_VM *vm, const struct lulu_Value *k);

#endif /* LULU_API_H */
