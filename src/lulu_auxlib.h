#ifndef LULU_AUXILLIARY_H
#define LULU_AUXILLIARY_H

#include <stdio.h>

#include "lulu.h"

/**=== AUXILLIARY LIBRARY ================================================== {{{
 *
 * Everything that follows is implemented purely in terms of the API.
 * You could very much reimplement these how you like; they are implemented
 * purely for convenience.
 */


typedef struct {
    const char    *name;
    lulu_CFunction function;
} lulu_Register;

typedef struct {
    lulu_VM *vm;
    size_t   cursor; /* Index of current position in the buffer. */
    int      pushed; /* How many things have were pushed onto the stack? */
    char     data[LULU_BUFFER_BUFISZE];
} lulu_Buffer;

#define lulu_type_name_at(vm, i)    lulu_type_name(vm, lulu_type(vm, i))

LULU_API void
lulu_buffer_init(lulu_VM *vm, lulu_Buffer *b);

LULU_API void
lulu_buffer_write_char(lulu_Buffer *b, char ch);

LULU_API void
lulu_buffer_write_string(lulu_Buffer *b, const char *s);

LULU_API void
lulu_buffer_write_lstring(lulu_Buffer *b, const char *s, size_t n);

LULU_API void
lulu_buffer_finish(lulu_Buffer *b);

LULU_API int
LULU_ATTR_PRINTF(4, 5)
lulu_arg_error(lulu_VM *vm, int argn, const char *whom, const char *fmt, ...);

LULU_API int
LULU_ATTR_PRINTF(2, 3)
lulu_errorf(lulu_VM *vm, const char *fmt, ...);


/**
 * @brief
 *  -   Registers all functions in `library[0:n]` to global table `libname`.
 * 
 * @param libname
 *      The name of the table to be used as the library module. Pass `NULL` to
 *      use the globals table instead.
 *
 *      If non-`NULL`, it is looked up in the global namespace. If the table
 *      does not exist, it is then created. Otherwise it is reused.
 */
LULU_API void
lulu_set_library(lulu_VM *vm, const char *libname,
    const lulu_Register *library, int n);

/*=== }}} =================================================================== */

#endif /* LULU_AUXILLIARY_H */
