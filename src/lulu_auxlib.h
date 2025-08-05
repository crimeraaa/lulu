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
    lulu_C_Function function;
} lulu_Register;

typedef struct {
    lulu_VM *vm;
    size_t   cursor; /* Index of current position in the buffer. */
    int      pushed; /* How many things have were pushed onto the stack? */
    char     data[LULU_BUFFER_BUFSIZE];
} lulu_Buffer;

LULU_API void
lulu_buffer_init(lulu_VM *vm, lulu_Buffer *b);

LULU_API void
lulu_write_char(lulu_Buffer *b, char ch);

LULU_API void
lulu_write_string(lulu_Buffer *b, const char *s);

LULU_API void
lulu_write_lstring(lulu_Buffer *b, const char *s, size_t n);

#define lulu_write_literal(b, s)  lulu_write_lstring(b, s, sizeof(s) - 1)

LULU_API void
lulu_finish_string(lulu_Buffer *b);

LULU_API int
lulu_arg_error(lulu_VM *vm, int argn, const char *msg);

LULU_API int
lulu_type_error(lulu_VM *vm, int argn, const char *type_name);

LULU_API void
lulu_check_type(lulu_VM *vm, int argn, lulu_Type type);


/**
 * @brief
 *      Asserts that the value at stack slot `argn` has a value, including
 *      `nil`. Will throw if `argn` is out of bounds of the stack.
 */
LULU_API void
lulu_check_any(lulu_VM *vm, int argn);

/**
 * @brief
 *  -   Asserts that the stack slot `argn` is of type `boolean`. If it is
 *      not, then an error is thrown and an error message is pushed.
 *
 * @note(2025-07-21)
 *
 *  -   This will work with negative indexes, although the error message may
 *      be misleading as 'negative' arguments are confusing conceptually.
 */
LULU_API int
lulu_check_boolean(lulu_VM *vm, int argn);


/**
 * @brief
 *  -   Asserts that the stack slot `argn` is of type `number` or a `string`
 *      that can be parsed into a number. If it is not, then an error is
 *      thrown and an error message is pushed.
 *
 * @return
 *  -   The `number` representation of the value at stack slot `argn` when
 *      successful.
 */
LULU_API lulu_Number
lulu_check_number(lulu_VM *vm, int argn);


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
 *      If the `number` value cannot be fully represented as an integer then
 *      it is truncated in some unspecified way.
 */
LULU_API lulu_Integer
lulu_check_integer(lulu_VM *vm, int argn);


/**
 * @brief
 *  -   Asserts that the stack slot `argn` is of type `string` or a `number`
 *      which is then converted to a string. If it is not, then an error is
 *      thrown and an error message is pushed.
 *
 * @return
 *  -   The `string` representation of the value at stack slot `argn` when
 *      successful.
 */
LULU_API const char *
lulu_check_lstring(lulu_VM *vm, int argn, size_t *n);


/**
 * @param def
 *      The default value to use if the stack slot `argn` is invalid (i.e.
 *      out of bounds or `nil`).
 */
LULU_API lulu_Number
lulu_opt_number(lulu_VM *vm, int argn, lulu_Number def);

/**
 * @param def
 *      The default value to use if the stack slot `argn` is invalid (i.e.
 *      out of bounds) or `nil`.
 */
LULU_API lulu_Integer
lulu_opt_integer(lulu_VM *vm, int argn, lulu_Integer def);


/**
 * @param def
 *      The default value to use if the stack slot `argn` is invalid (i.e.
 *      out of bounds) or `nil`.
 *
 * @param n
 *      Optional out-parameter to contain the string length.
 */
LULU_API const char *
lulu_opt_lstring(lulu_VM *vm, int argn, const char *def, size_t *n);


LULU_API int
LULU_ATTR_PRINTF(2, 3)
lulu_errorf(lulu_VM *vm, const char *fmt, ...);


/**
 * @brief
 *  -   Registers all functions in `library[0:n]` to global table `libname`,
 *      or the table on top of stack.
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
LULU_API void
lulu_set_library(lulu_VM *vm, const char *libname,
    const lulu_Register *library, int n);



/*=== }}} =============================================================== */

/*=== HELPER MACROS ================================================= {{{ */


#define lulu_type_name_at(vm, i)        lulu_type_name(vm, lulu_type(vm, i))
#define lulu_check_string(vm, argn)     lulu_check_lstring(vm, argn, NULL)
#define lulu_opt_string(vm, argn, def)  lulu_opt_lstring(vm, argn, def, NULL)


/**
 * @brief
 *      Because the count is explicitly passed to `lulu_set_library()`, use
 *      this macro to aid in automatically getting the number of items given
 *      a particular `lulu_Register library[N]`.
 */
#define lulu_count_library(libs)    (int)(sizeof(libs) / sizeof(libs[0]))

/*=== }}} =============================================================== */

LULU_API void
lulu_open_libs(lulu_VM *vm);

LULU_API int
lulu_open_base(lulu_VM *vm);

LULU_API int
lulu_open_string(lulu_VM *vm);

#endif /* LULU_AUXILLIARY_H */
