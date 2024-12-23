#ifndef LULU_STRING_H
#define LULU_STRING_H

#include "object.h"

/**
 * @brief   A string of known length. Not necessarily nul-terminated.
 *
 * @note    This is mainly for the `LULU_TOKEN_STRINGS` as we would rather
 *          not need to constantly use `strlen` at runtime. Otherwise, you should
 *          prefer to just track the `data` and `len` pairs yourself.
 */
typedef struct {
    const char *data;
    isize       len;
} LString;

/**
 * @warning 2024-12-10 Rely on extensions here!
 */
#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wc99-extensions"
#elif defined _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4200)
#endif

struct lulu_String {
    lulu_Object base;
    isize       len;    // Number of non-nul characters.
    u32         hash;
    char        data[]; // Guaranteed to be nul terminated.
};

#if defined(__GNUC__)
    #pragma GCC diagnostic pop
#elif defined _MSC_VER
    #pragma warning(pop)
#endif

void
lulu_String_init(lulu_String *self, isize len, u32 hash);

// Analogous to the book's object.c:allocateString().
lulu_String *
lulu_String_new(lulu_VM *vm, const char *data, isize len);

// Since self is a variable length structure pointer, we free it here.
void
lulu_String_free(lulu_VM *vm, lulu_String *self);

u32
lulu_String_hash(const char *data, isize len);

#endif // LULU_STRING_H
