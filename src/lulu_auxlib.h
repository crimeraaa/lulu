#ifndef LULU_AUXILLIARY_H
#define LULU_AUXILLIARY_H

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


LULU_API int
#if defined(__GNUC__) || defined(__clang__)
__attribute__ ((__format__ (__printf__, 2, 3)))
#endif
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
    const lulu_Register *library, size_t n);

/*=== }}} =================================================================== */

#endif /* LULU_AUXILLIARY_H */
