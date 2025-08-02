#ifndef LULU_H
#define LULU_H

#include <stdarg.h>
#include <stddef.h>

#include "lulu_config.h"

typedef struct lulu_VM    lulu_VM;
typedef struct lulu_Debug lulu_Debug;
typedef LULU_NUMBER_TYPE  lulu_Number;
typedef LULU_INTEGER_TYPE lulu_Integer;


/**
 * @brief
 *      'pseudo' indexes are never valid relative stack indexes. That is,
 *      no negative relative stack index should ever be less than this.
 *
 *      Instead, they allow us to manipulate specific parts of the VM, such
 *      as the globals table. In Lua they also allow us to poke at the
 *      registry, the environment, and upvalues.
 */
#define LULU_PSEUDO_INDEX   (-15000)
#define LULU_GLOBALS_INDEX  (LULU_PSEUDO_INDEX)

/**
 * @brief LULU_API
 *      Defines the name linkage for functions. C++ by default uses
 *      'name-mangling', which allows multiple declarations of the same
 *      function name to have different parameter lists.
 *
 *      Name mangling makes it impossible for C relilably link to C++.
 *      So using `extern "C"` specifies that, when compiling with a C++
 *      compiler, no name mangling should occur. Thus C programs can
 *      properly link to our shared library.
 */
#ifdef __cplusplus
#define LULU_API    extern "C" LULU_PUBLIC
#else   /* ^^^ __cplusplus, vvv otherwise */
#define LULU_API    extern LULU_PUBLIC
#endif /* __cplusplus */


/**
 * @brief LULU_FUNC
 *      This controls the visibility of externally visible functions that
 *      are not part of the API.
 *
 *      That is, you can define a function like so: `LULU_FUNC void f();`
 *      `f` will not be exported but it is still visible to all functions
 *      within the library that include `f`'s header.
 *
 * @brief LULU_DATA
 *      Similar to `LULU_FUNC`, but intended for data. e.g.
 *      `LULU_DATA const char *const tokens[TOKEN_COUNT];`
 */
#define LULU_FUNC       extern LULU_PRIVATE
#define LULU_DATA       extern LULU_PRIVATE


/**
 * @brief
 *      Argument to `lulu_call()` and `lulu_pcall()` to indicate that
 *      an unknown number of values will be returned by this particular
 *      function call.
 */
#define LULU_MULTRET    (-1)


/**
 * @brief
 *      A protocol that defines how memory is allocated within Lulu.
 *
 * @note(2025-08-02)
 *  The allocator must fulfill the following properties:
 *
 *  1.) The returned pointer is suitably aligned for the host implementation.
 *      Alignment is not passed explicitly; similar to `malloc()` the
 *      worst-case alignment should be assumed.
 *
 *  2.) If `ptr == NULL` and `new_size != 0`, then this function acts
 *      similarly to the C standard `malloc(new_size)`. The resulting
 *      pointer must be guaranteed to be unique from any other pointer.
 *
 *  3.) If `ptr != NULL` and `new_size == 0`, then this function acts
 *      similarly to the C standard `realloc(ptr, new_size)`.
 *      The resulting pointer may alias `ptr` if the allocation can be
 *      extended, thus it is not guaranteed to be unique.
 *
 *  4.) If `ptr == NULL` and `new_size != 0`, then this function
 *      acts similarly to the C standard `free(ptr)`. `NULL`
 *      is returned as a sentinel value in this case.
 */
typedef void *
(*lulu_Allocator)(void *user_ptr, void *ptr, size_t old_size, size_t new_size);


/**
 * @brief
 *      The protocol that allows for C to interface with Lulu.
 *      The number of arguments can be obtained via `lulu_get_top()`.
 *      The only way to manipulate Lulu is via the API.
 *
 * @return
 *      The number of values pushed to the stack that will be used by the
 *      caller.
 */
typedef int
(*lulu_CFunction)(lulu_VM *vm);


/**
 * @brief
 *      A generic interface to allow optimized reading of scripts/files.
 *      This is mainly to allow implementation of buffered reading
 *      for files received from the `fopen()` family.
 *
 * @param user_ptr
 *      A pointer to an arbitrary type that you, the user, allocated
 *      somewhere else (e.g. on the stack). The internal implementation
 *      guarantees that this pointer is only ever passed to calls of the
 *      function it was registered with, so casting should be safe.
 *
 * @param n
 *      An out-parameter which will hold the length of the data read.
 *      The internal implementation guarantees this to be non-`NULL`.
 *
 * @return
 *      A read-only pointer to some character buffer in `data.` How the
 *      buffer is managed is up to you. This may be `NULL`.
 */
typedef const char *
(*lulu_Reader)(void *user_ptr, size_t *n);


/**
 * @brief 2025-06-11
 *  -   Chapter 15.1.1 of Crafting Interpreters: "Executing instructions".
 */
typedef enum {
    LULU_OK,
    LULU_ERROR_SYNTAX,
    LULU_ERROR_RUNTIME,
    LULU_ERROR_MEMORY
} lulu_Error;


/**
 * @brief 2025-06-16
 *  -   Chapter 18.1 of Crafting Interpreters: "Tagged Unions".
 */
typedef enum {
    LULU_TYPE_NONE = -1 ,       /* out of bounds stack index, C API only. */
    LULU_TYPE_NIL,
    LULU_TYPE_BOOLEAN,
    LULU_TYPE_LIGHTUSERDATA,    /* A non-collectible C pointer. */
    LULU_TYPE_NUMBER,
    LULU_TYPE_STRING,
    LULU_TYPE_TABLE,
    LULU_TYPE_FUNCTION          /* A Lua or C function. */
} lulu_Type;

LULU_API lulu_VM *
lulu_open(lulu_Allocator allocator, void *allocator_data);


/**
 * @brief
 *      Sets the VM's global panic function, which is called when
 *      errors are thrown outside of protected calls.
 */
LULU_API lulu_CFunction
lulu_set_panic(lulu_VM *vm, lulu_CFunction panic_fn);

LULU_API void
lulu_close(lulu_VM *vm);


/**
 * @brief
 *      Compiles the script read in by `reader` into a Lua function,
 *      which is pushed to the top of the stack.
 */
LULU_API lulu_Error
lulu_load(lulu_VM *vm, const char *source, lulu_Reader reader,
    void *reader_data);


/**
 * @brief
 *      Calls the Lua or C function at the relative stack index
 *      `-(n_args + 1)` with arguments from `(nargs)` up to and
 *      including `-1`.
 *
 * @note(2025-06-30)
 *      The function and its arguments are popped when done.
 *
 *      The return values, if any, are moved to where the function
 *      originally was. That is, the `-(n_args + 1)` stack slot, up to the
 *      `n_ret` slot above it, is overwritten.
 *
 *      If the function returned less than `n_rets` values then remaining
 *      slots are set to `nil`.
 */
LULU_API void
lulu_call(lulu_VM *vm, int n_args, int n_rets);


/**
 * @brief
 *  -   Wraps `lulu_call()` in a protected call so that we may catch any thrown
 *      exceptions. See the notes there regarding how the stack is managed
 *      before and after the call.
 *
 * @return
 *  -   The error code, if any was thrown, or else `LULU_OK`.
 */
LULU_API lulu_Error
lulu_pcall(lulu_VM *vm, int n_args, int n_rets);


/**
 * @brief
 *  -   Wraps the call `function(vm, function_data)` in a protected call
 *      so that we may catch any thrown exceptions.
 *
 *  -   At the start of the call, `function_data` is the 1st (and only)
 *      argument present on the stack. It can be retrieved by via a call
 *      to `lulu_to_userdata(vm, 1)`.
 *
 * @return
 *  -   The error code, if any was thrown, or else `LULU_OK`.
 */
LULU_API lulu_Error
lulu_c_pcall(lulu_VM *vm, lulu_CFunction function, void *function_data);


/**
 * @brief
 *  -   Throws a runtime error no matter what.
 *
 * @note(2025-07-01)
 *
 *  -   This function never returns, but as in the Lua 5.1 API a common idiom
 *      within C functions is to do `return lulu_error();`
 *
 *  -   This function on its own does not push an 'error object' such as a
 *      string. You may wish to call `lulu_push_fstring()` beforehand.
 */
LULU_API int
lulu_error(lulu_VM *vm);


/*=== TYPE QUERY FUNCTIONS ========================================== {{{ */


/**
 * @return
 *  -   The type tag of the value at relative stack index `i`.
 *
 * @note(2025-07-20)
 *
 *  -   If `i` is out of bounds then `LULU_TYPE_NONE` is returned.
 */
LULU_API lulu_Type
lulu_type(lulu_VM *vm, int i);


/**
 * @return
 *  -   The nul-terminated type-name of the type tag `t`.
 *
 * @note(2025-07-20)
 *
 *  -   C does not allow overloading of functions, and even if had, implicit
 *      conversions may lead to ambiguity if the compiler treats `lulu_Type`
 *      as being the same as `int`.
 *
 *  -   Thus to get the type-name of the value at a particular index `i`,
 *      you would need to first call `lulu_type()` and pass it to here. E.g.
 *      `const char *s = lulu_type_name(vm, lulu_type(vm, -1));`;
 */
LULU_API const char *
lulu_type_name(lulu_VM *vm, lulu_Type t);


/**
 * @return
 *  -   `1` if the value at relative stack index `i` is a `number` or a
 *      `string` which represents a valid number, or else `0`.
 */
LULU_API int
lulu_is_number(lulu_VM *vm, int i);


/**
 * @return
 *  -   `1` if the value at relative stack index `i` is a `string` or a
 *      `number` (which is always convertible to a string), else `0`.
 *
 * @todo(2025-07-20)
 *
 *  -   Lua also considers `number` to be `string` since they are very easy to
 *      immediately convert.
 *
 *  -   This is because `lua_tolstring()` converts the stack slot to the string
 *      representation of the number.
 */
LULU_API int
lulu_is_string(lulu_VM *vm, int i);


/**
 * @return
 *  -   `1` if the value at relative stack index `i` is truthy, else `0` if it
 *      is falsy.
 *
 * @note(2025-07-20)
 *  -   No conversions in the stack occur.
 */
LULU_API int
lulu_to_boolean(lulu_VM *vm, int i);


/**
 * @return
 *  -   The `number` representation at the relative stack index `i` or else `0`.
 *
 *  -   `number` is read directly while `string` is checked then parsed.
 *      All other types return `0`.
 *
 * @note(2025-07-20)
 *  -   Returning `0` is not enough to indicate the value was not of type
 *      `number`, because it is possible for the number `0` to be in the stack.
 *
 *  -   You must call `lulu_is_number()` beforehand in that case.
 */
LULU_API lulu_Number
lulu_to_number(lulu_VM *vm, int i);


/**
 * @return
 *      The `number` representation of the value at the relative stack index
 *      `i`, or else `0`. The same conversion rules in `lulu_to_number()` apply.
 *
 * @note(2025-07-21)
 *
 *  -   If the value at `i` is a number but cannot be accurately represented
 *      as a `lulu_Integer` (e.g. because it is a floating-point type), then
 *      the result is truncated in some unspecified way.
 */
LULU_API lulu_Integer
lulu_to_integer(lulu_VM *vm, int i);


/**
 * @brief
 *      Converts the value at relative stack index `i` to a `string`, if
 *      possible.
 *
 *      Values of type `string` are read directly, while values of type
 *      `number` are replaced with their string representation. All other
 *      types are unchanged.
 *
 * @param n
 *      Optional out-parameter which stores the resulting string's length,
 *      if the value was indeed a string. Pass `NULL` to ignore.
 *
 * @return
 *      The nul-terminated string at relative stack index `i`, or else `NULL`.
 */
LULU_API const char *
lulu_to_lstring(lulu_VM *vm, int i, size_t *n);


/**
 * @return
 *      The `void *` representation at relative stack index `i`, or else
 *      `NULL`.
 *
 * @note(2025-07-20)
 *
 *      `NULL` may be returned if the value cannot be suitably converted to a
 *      pointer. The only convertible types are either `userdata` or 'true'
 *      objects such as `table` or `function`.
 *
 *      `string` is not a 'true' object because it is immmutable and always
 *      passed by value conceptually.
 */
LULU_API void *
lulu_to_pointer(lulu_VM *vm, int i);


/*=== }}} =============================================================== */

/*=== STACK MANIPULATION FUNCTIONS ================================= {{{ */


/**
 * @brief
 *  -   Retrieves the number of elements in the current stack frame.
 */
LULU_API int
lulu_get_top(lulu_VM *vm);


/**
 * @brief
 *  -   Sets the end of the current stack frame to the relative stack index `i`.
 *
 * @param i
 *      When `0`, effectively pops all the elements as the stack frame will
 *      have a length of 0.
 *
 *      When `> 0`, only `i` values remain in the stack. E.g. `i == 1` will
 *      pop all elements until absolute index `1`, leaving it to be the new
 *      top of the stack.
 *
 *      When `< 0`, only `lulu_get_top(vm) - (-i)` values remain in the stack.
 *      E.g. `i == -1` will pop all elements from the top of the stack, going
 *      down and including `i`. The value at `i` itself is also popped.
 *
 * @note(2025-07-20)
 *
 *  -   `i` cannot be a pseudo index.
 */
LULU_API void
lulu_set_top(lulu_VM *vm, int i);


/**
 * @brief
 *  -   Replaces the valule at the stack index `i` with the top of the stack
 *      and shifts all elements that were above `i` up by one index.
 *
 * @note(2025-07-20)
 *
 *  -   `i` cannot be a pseudo index, because it would not refer to an actual
 *      position in the stack.
 */
LULU_API void
lulu_insert(lulu_VM *vm, int i);


/**
 * @brief
 *  -   Removes the value at the stack index `i`, and shift all elements that
 *      were above `i` down by one index.
 *
 * @note(2025-07-20)
 *
 *  -   `i` cannot be a pseudo index, because it would not refer to an actual
 *      position in the stack.
 */
LULU_API void
lulu_remove(lulu_VM *vm, int i);


/**
 * @brief
 *  -   Pops `n` values from the top of the stack.
 */
LULU_API void
lulu_pop(lulu_VM *vm, int n);


/**
 * @brief
 *  -   Pushes `n` `nil` values to the top of the stack.
 */
LULU_API void
lulu_push_nil(lulu_VM *vm, int n);


/**
 * @brief
 *  -   Pushes `b` to the top of the stack as a `boolean` value.
 */
LULU_API void
lulu_push_boolean(lulu_VM *vm, int b);


/**
 * @brief
 *  -   Pushes `n` to the top of the stack as a `number` value.
 */
LULU_API void
lulu_push_number(lulu_VM *vm, lulu_Number n);


/**
 * @brief
 *  -   Pushes `i` to the top of the stack as a `number` value.
 *
 * @note(2025-07-21)
 *
 *  -   If `i` cannot be represented accurately as a `lulu_Number`, it may
 *      be truncated (e.g. when `lulu_Number` is floating-point type).
 */
LULU_API void
lulu_push_integer(lulu_VM *vm, lulu_Integer i);


/**
 * @brief
 *  -   Pushes `p` to the top of the stack as a `userdata` value.
 */
LULU_API void
lulu_push_userdata(lulu_VM *vm, void *p);


/**
 * @brief
 *  -   Pushes the string `s`, bounded by length `n`, to the top of the stack.
 */
LULU_API void
lulu_push_lstring(lulu_VM *vm, const char *s, size_t n);


/**
 * @brief
 *  -   Pushes the nul-terminated string `s` to the top of the stack.
 */
LULU_API void
lulu_push_string(lulu_VM *vm, const char *s);


/**
 * @brief
 *  -   Pushes a formatted string to the top of the stack, following a
 *      simplified version of C `printf`.
 *
 * @param fmt
 *      The format string. Only specifiers in the regex `%[cdifsp]` are allowed.
 *      No precision, widths, or any modifiers are implemented.
 */
LULU_API const char *
LULU_ATTR_PRINTF(2, 3)
lulu_push_fstring(lulu_VM *vm, const char *fmt, ...);


/**
 * @brief
 *  -   `va_list` wrapper similar to `lulu_push_fstring()`, following the exact
 *      same rules.
 */
LULU_API const char *
lulu_push_vfstring(lulu_VM *vm, const char *fmt, va_list args);


/**
 * @brief
 *  -   Pushes `cf` to the top of the stack as a `function` value.
 */
LULU_API void
lulu_push_cfunction(lulu_VM *vm, lulu_CFunction cf);


/**
 * @brief
 *  -   Pushes a copy of the value at the relative/pseudo stack index `i`
 *      to the top of the stack.
 */
LULU_API void
lulu_push_value(lulu_VM *vm, int i);

/*=== }}} =============================================================== */


LULU_API void
lulu_new_table(lulu_VM *vm, int n_array, int n_hash);


/**
 * @brief
 *  -   Pops `key` from the stack and pushes `table[key]` in its place.
 *
 * @param table_index
 *  -   The relative/pseudo stack index of the table we wish to index.
 *
 * @return
 *      `1` if the key existed in the table, meaning its value was pushed.
 *      `0` otherwise, meaning `nil` was pushed.
 *
 * @note(2025-07-20) Assumptions
 *
 *  1.) The key to be used is at relative stack index `-1`.
 *
 *  2.) It is possible for `table[key]` to exist (e.g. mapped previously) but
 *      for it to map to `nil. E.g. `t[k] = 1; ...; t[k] = nil;`
 */
LULU_API int
lulu_get_table(lulu_VM *vm, int table_index);


/**
 * @brief
 *  -   Pushes `table[key]` to the stack.
 *
 * @param table_index
 *      The relative/psuedo stack index of the table we wish to index.
 *
 * @param key
 *      A nul-terminated C string representing the field name we wish to get.
 *
 * @return
 *      `1` if `key` existed in the table, meaning `table[key]` was pushed.
 *      `0` otherwise, meaning `nil` was pushed.
 *
 * @note(2025-07-20)
 *  -   It is possible for `table[key]` to have been mapped previously and
 *      to map to `nil`.
 */
LULU_API int
lulu_get_field(lulu_VM *vm, int table_index, const char *key);


/**
 * @param table_index
 *      The relative/psuedo stack index of the table we wish to set.
 *
 * @note(2025-07-20) Assumptions
 *
 *  1.) The key is at stack index `-2` and the value is at stack index `-1.`
 *  2.) When this operation is done, both the key and value are popped.
 */
LULU_API void
lulu_set_table(lulu_VM *vm, int table_index);


/**
 * @param table_index
 *      The relative/psuedo stack index of the table we wish to set.
 *
 * @param key
 *      A nul-terminated C string representing the field we wish to set.
 *
 * @note(2025-07-20) Assumptions
 *
 *  1.) The value to be used is at the relative stack index `-1`. When this
 *      operation is done, the value is popped.
 */
LULU_API void
lulu_set_field(lulu_VM *vm, int table_index, const char *key);


/**
 * @brief
 *  -   Gets the key `s` from the VM globals table and pushes it to the current
 *      top of the stack.
 *
 *  -   If the key did not exist, then `nil` is pushed.
 *
 * @return
 *  -   `0` if `s` does not exist in the globals table, else `1`.
 */
#define lulu_get_global(vm, key)    lulu_get_field(vm, LULU_GLOBALS_INDEX, key)


/**
 * @brief
 *      Sets the global variable with key `s` to the current top of the stack.
 *      The value is then popped.
 */
#define lulu_set_global(vm, key)    lulu_set_field(vm, LULU_GLOBALS_INDEX, key)


/**
 * @brief
 *  -   Perform string concatenation from the `-(n)` up to and including `-1`
 *      stack indexes.
 *
 *  -   The `-(n)`th slot is overwritten with the result.
 *
 * @note(2025-06-30) Assumptions
 *
 *  1.  The stack has at least `n` elements such that doing pointer arithmetic
 *      is vaild.
 */
LULU_API void
lulu_concat(lulu_VM *vm, int n);


/**
 * @brief
 *      Gets the length of the value at the relative stack index `i`. Only
 *      works for values of type `string` and `table`.
 *
 * @return
 *      The length of the object, else `0`.
 */
LULU_API size_t
lulu_obj_len(lulu_VM *vm, int i);


/**
 * @param level
 *      How far up the call stack you wish to get the debug information of.
 *      `0` is the caller of this function (e.g. a C function),
 *      `1` is the caller of caller (e.g. Lua main function calling C) etc.
 *
 * @return
 *      `1` if successfully filled in `*ar` else `0`.
 */
LULU_API int
lulu_get_stack(lulu_VM *vm, int level, lulu_Debug *ar);


/**
 * @param options
 *      A nul-terminated string consisting of any combination of these
 *      characters:
 *      `'n'` (to select `name` and `namewhat`).
 *      `'S'` (to select `source`, `what` and `linedefined`)
 *      `'l'` (to select `currentline`)
 */
LULU_API int
lulu_get_info(lulu_VM *vm, const char *options, lulu_Debug *ar);


/** HELPER MACROS =================================================== {{{ */


/**
 * @return
 *  -   `1` if the value at relative stack index `i` is `nil`, else `0`.
 *
 * @note(2025-07-19)
 *
 *  -   For C functions called from Lua, since function arity (parameter count)
 *      is not known beforehand, arguments are not default-initalized to `nil`
 *      if not provided as the implementation has no way of knowing how many
 *      arguments you wanted.
 *
 *  -   Thus, to check for arguments that were not provided at all from a Lua
 *      function, use `lulu_is_none()`.
 *
 *  -   C functions called from C (as in `lulu_pcall()`) do know how many
 *      arguments to expect because they were explicitly passed.
 */
#define lulu_is_nil(vm, i)          (lulu_type(vm, i) == LULU_TYPE_NIL)


/**
 * @return
 *  -   `1` if the value at relative stack index `i` is `none` else `0`.
 *
 * @note(2025-07-19)
 *
 *  -   Only indexes outside the current stack frame are type `none`. Only
 *      C functions can see them because of their unsafe capability to request
 *      any stack index.
 */
#define lulu_is_none(vm, i)         (lulu_type(vm, i) == LULU_TYPE_NONE)


/**
 * @return
 *      `1` if the value at relative stack index `i` is `none` or `nil`,
 *      else `0`.
 */
#define lulu_is_none_or_nil(vm, i)  (lulu_type(vm, i) <= LULU_TYPE_NIL)


/**
 * @return
 *  -   `1` if the value at relative stack index `i` is a `boolean`, else `0`.
 */
#define lulu_is_boolean(vm, i)      (lulu_type(vm, i) == LULU_TYPE_BOOLEAN)


/**
 * @return
 *  -   `1` if the value at relative stack index `i` is a `userdata`, else `0`.
 *
 * @note(2025-07-20)
 *
 *  -   `userdata` are generally safe to cast to pointers of some type `T`.
 *      A good example is the callback function of `lulu_c_pcall()`, whose
 *      1 and only argument is the userdata to be used.
 *
 *  -   Of course, the safety of casting to and from `void *` cannot be
 *      guaranteed by your compiler, much less `lulu`.
 */
#define lulu_is_userdata(vm, i)     (lulu_type(vm, i) == LULU_TYPE_LIGHTUSERDATA)


/**
 * @return
 *  -   `1` if the value at relative stack index `i` is a `table`, else `0`.
 */
#define lulu_is_table(vm, i)        (lulu_type(vm, i) == LULU_TYPE_TABLE)


/**
 * @return
 *  -   `1` if the value at relative stack index `i` is a `function`, else `0`.
 */
#define lulu_is_function(vm, i)     (lulu_type(vm, i) == LULU_TYPE_FUNCTION)


/**
 * @brief
 *  -   In C, it is very common to use nul-terminated `const char *` to act as
 *      the primary 'string' type. The `str*` functions in the C standard
 *      library, for example, always assume their inputs are nul-terminated.
 *
 *  -   So this macro simply calls `lulu_to_lstring()` with `n == NULL` to
 *      avoid explicitly storing the length anywhere.
 *
 * @note(2025-07-19)
 *  -   The internal implementation of `lulu` ensures that all strings are nul
 *      terminated.
 */
#define lulu_to_string(vm, i)       lulu_to_lstring(vm, i, NULL)

/**
 * @brief
 *  -   In C, it is very common to use string literals denoted by `"<text>"`.
 *
 *  -   Instead of calling `lulu_push_string(vm, s)` thus incurring the
 *      function call to `strlen()` that can be skipped if the string size is
 *      known at compile time.
 */
#define lulu_push_literal(vm, s)    lulu_push_lstring(vm, s, sizeof(s) - 1)


/**
 * @brief
 *  -   A fairly common operation is to bind a function to a global name.
 *
 *  -   A good example of this is the base library; the functions `print()`,
 *      `type()`, `tostring()` are all merely global identifiers that happen to
 *      reference C functions.
 */
#define lulu_register(vm, name, cfunction) \
    (lulu_push_cfunction(vm, cfunction), lulu_set_global(vm, name))

/*== }}} ================================================================ */


/**
 * @brief
 *      An activation record.
 *
 * @link
 *      https://www.lua.org/pil/23.1.html
 *
 * @note(2025-07-23)
 *      For consistency, we name the fields exactly as they would appear in
 *      the table returned by `debug.getinfo()`.
 */
struct lulu_Debug {
    const char *name;       /* (n) variable name for this function. */
    const char *namewhat;   /* (n) `"global"`, `"local"`, `"field"` or `""` */
    const char *what;       /* (S) `Lua`, `C`, `main` */
    const char *source;     /* (S) file name where we originated from. */
    int currentline;        /* (l) line number at point of calling.*/
    int linedefined;        /* (S) first line in source code (opt.) */
    int lastlinedefined;    /* (S) last line in source code (opt.) */

    /* private to implementation. No poking around! */
    int _cf_index;
};

#endif /* LULU_H */
