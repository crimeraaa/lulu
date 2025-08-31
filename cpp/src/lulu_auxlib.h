#ifndef LULU_AUXILLIARY_H
#define LULU_AUXILLIARY_H

#include <stdio.h>

#include "lulu.h"

/**=== AUXILLIARY LIBRARY ============================================== {{{
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
    lulu_VM *L;

    /* Index of current writeable position in `data`. */
    size_t cursor;

    /* How many intermediate strings have we pushed so far? */
    int pushed;

    /* The underlying buffer data. */
    char data[LULU_BUFFER_BUFSIZE];
} lulu_Buffer;

LULU_LIB_API void
lulu_buffer_init(lulu_VM *L, lulu_Buffer *b);

LULU_LIB_API void
lulu_write_char(lulu_Buffer *b, char ch);

LULU_LIB_API void
lulu_write_string(lulu_Buffer *b, const char *s);

LULU_LIB_API void
lulu_write_lstring(lulu_Buffer *b, const char *s, size_t n);

#define lulu_write_literal(b, s) lulu_write_lstring(b, s, sizeof(s) - 1)

LULU_LIB_API void
lulu_finish_string(lulu_Buffer *b);

LULU_LIB_API int
lulu_arg_error(lulu_VM *L, int argn, const char *msg);

LULU_LIB_API int
lulu_type_error(lulu_VM *L, int argn, const char *type_name);

LULU_LIB_API void
lulu_check_type(lulu_VM *L, int argn, lulu_Type type);


/** @brief Asserts stack slot `argn` is anything but `none`.
 *
 * @details `nil` is allowed, so we throw only if `argn` is out of bounds.
 */
LULU_LIB_API void
lulu_check_any(lulu_VM *L, int argn);


/** @brief Asserts stack slot `argn` is of type `boolean`. */
LULU_LIB_API int
lulu_check_boolean(lulu_VM *L, int argn);


/** @brief Asserts stack slot `argn` is of type `number` or a `string` that can
 *  be parsed into one.
 */
LULU_LIB_API lulu_Number
lulu_check_number(lulu_VM *L, int argn);


/**
 * @brief
 *      Asserts that the absolute stack slot `argn` is of type `number` or
 *      a `string` that can be parsed into a number. If neither condition is
 *      met, then an error is thrown and an error message is pushed.
 *
 * @return
 *      The `number` representation of the value at stack slot `argn`,
 *      converted to an integer, when successful.
 *
 * @note(2025-07-24)
 *      If the `number` value cannot be accurately represented as an
 *      integer then it is truncated as per the C standard.
 */
LULU_LIB_API lulu_Integer
lulu_check_integer(lulu_VM *L, int argn);


/** @brief Asserts that the stack slot `argn` is `string` or a `number`
 *  (which is then converted to a string).
 *
 * @return
 *      The `string` representation of the value at stack slot `argn` when
 *      successful.
 */
LULU_LIB_API const char *
lulu_check_lstring(lulu_VM *L, int argn, size_t *n);


/**
 * @param def
 *      The default value to use if the stack slot `argn` is invalid (i.e.
 *      out of bounds or `nil`).
 */
LULU_LIB_API lulu_Number
lulu_opt_number(lulu_VM *L, int argn, lulu_Number def);


/**
 * @param def
 *      The default value to use if the stack slot `argn` is invalid (i.e.
 *      out of bounds) or `nil`.
 */
LULU_LIB_API lulu_Integer
lulu_opt_integer(lulu_VM *L, int argn, lulu_Integer def);


/**
 * @param def
 *      The default value to use if the stack slot `argn` is invalid (i.e.
 *      out of bounds) or `nil`.
 *
 * @param [out] n
 *      Optional. Will contain the string length.
 */
LULU_LIB_API const char *
lulu_opt_lstring(lulu_VM *L, int argn, const char *def, size_t *n);


LULU_LIB_API int LULU_ATTR_PRINTF(2, 3)
    lulu_errorf(lulu_VM *L, const char *fmt, ...);


/** @brief Registers all functions in `library[0:n]` to global table `libname`,
 *  or the table on top of stack. Assumes no upvalues.
 *
 * @param libname
 *      The name of the table to be used as the library module.
 *
 *      If `NULL`, then we assume that there is a table currently on top
 *      of the stack. We will register all functions in `library` to this
 *      table instead.
 *
 * @return
 *      This function leaves the module table on top of the stack.
 */
LULU_LIB_API void
lulu_set_nlibrary(lulu_VM *L, const char *libname,
    const lulu_Register *library, int n);


/*=== }}} =============================================================== */

/*=== HELPER MACROS ================================================= {{{ */


#define lulu_type_name_at(vm, i) lulu_type_name(vm, lulu_type(vm, i))

/** @brief Asserts that `expr` is true.
 *
 * @details
 *  If not, then an error is thrown and caught by the enclosing protected call.
 *  An error message is also pushed to the top of the stack.
 */
#define lulu_arg_check(vm, expr, argn, msg)                                    \
    if (!(expr)) {                                                             \
        lulu_arg_error(vm, argn, msg);                                         \
    }

#define lulu_check_string(vm, argn)    lulu_check_lstring(vm, argn, NULL)
#define lulu_opt_string(vm, argn, def) lulu_opt_lstring(vm, argn, def, NULL)
#define lulu_count_library(fns)        (int)(sizeof(fns) / sizeof((fns)[0]))

/** @brief Helper macro for for fixed-size array of `lulu_Register`.
 *
 * @param name
 *      Desired libary name. See notes in `lulu_set_library()` for more
 *      information.
 *
 * @param fns
 *      `lulu_Register[N]`, a fixed-size array.
 */
#define lulu_set_library(vm, name, fns)                                        \
    lulu_set_nlibrary(vm, name, fns, lulu_count_library(fns))

/*=== }}} =============================================================== */

LULU_LIB_API void
lulu_open_libs(lulu_VM *L);

LULU_LIB_API int
lulu_open_base(lulu_VM *L);

LULU_LIB_API int
lulu_open_math(lulu_VM *L);

LULU_LIB_API int
lulu_open_string(lulu_VM *L);

LULU_LIB_API int
lulu_open_os(lulu_VM *L);

#endif /* LULU_AUXILLIARY_H */
