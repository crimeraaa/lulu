#ifndef LULU_API_H
#define LULU_API_H

#include "lulu.h"

typedef struct Value Value;

// Negative values are offset from the top, positive are offset from the base.
Value *poke_at(VM *self, int offset);

void push_nils(VM *self, int n);
void push_boolean(VM *self, bool b);
void push_number(VM *self, Number n);

// Push a nul-terminated C-string of yet-to-be-determined length.
void push_cstring(VM *self, const char *s);

// Push a nul-terminated C-string of desired length.
void push_lcstring(VM *self, const char *s, int len);

// Internal use for writing simple formatted messages.
// Only accepts the following formats: i, d, s and p.
// No modifiers are allowed.
void push_vfstring(VM *self, const char *fmt, va_list argp);

// See `push_vfstring` for constraints.
void push_fstring(VM *self, const char *fmt, ...);

// `argv` is a 1D array, not a singular element.
void concat_op(VM *self, int argc, Value *argv);

#endif /* LULU_API_H */
