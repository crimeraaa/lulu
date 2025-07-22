#include <string.h>

#include "lulu_auxlib.h"

LULU_API void
lulu_buffer_init(lulu_VM *vm, lulu_Buffer *b)
{
    b->vm     = vm;
    b->cursor = 0;
    b->pushed = 0;
}


/**
 * @return
 *      The number of characters currently stored in the buffer.
 */
static size_t
_buffer_len(const lulu_Buffer &b)
{
    return b.cursor;
}


/**
 * @return
 *      The total number of characters that could be stored in the buffer.
 */
static constexpr size_t
_buffer_cap(const lulu_Buffer &b)
{
    return sizeof(b.data);
}


/**
 * @return
 *      The number of free slots in the buffer.
 */
static size_t
_buffer_rem(const lulu_Buffer &b)
{
    return _buffer_cap(b) - _buffer_len(b);
}

static bool
_buffer_flushed(lulu_Buffer *b)
{
    size_t n = _buffer_len(*b);
    // Nothing to put on the stack?
    if (n == 0) {
        return false;
    }
    lulu_push_lstring(b->vm, b->data, n);
    b->cursor = 0;
    b->pushed++;
    return true;
}


static constexpr int LIMIT = LULU_STACK_MIN / 2;


/**
 * @brief
 *  -   Concatenate some of our temporary strings if it can be helped so that
 *      we do not overflow the VM stack.
 */
static void
_buffer_adjust_stack(lulu_Buffer *b)
{
    // More than 1 string previously pushed, so we need to manage them.
    if (b->pushed > 1) {
        lulu_VM *vm = b->vm;
        int to_concat = 1; // Number of levels to concatenate.

        // Accumulator length for all the temporaries we will concatenate.
        // Starts with the one currently on the top of the stack.
        size_t acc_len = lulu_obj_len(vm, -1);

        // Assumes that since `b->pushed > 1`, we have at least 2 strings.
        do {
            size_t here_len = lulu_obj_len(vm, -(to_concat + 1));

            // We have too many strings OR our total length exceeds this one?
            if (b->pushed - to_concat + 1 >= LIMIT || acc_len > here_len) {
                acc_len += here_len;
                to_concat++;
            } else {
                break;
            }
        } while (to_concat < b->pushed);
        lulu_concat(vm, to_concat);
        b->pushed = b->pushed - to_concat + 1;
    }
}

static void
_buffer_prep(lulu_Buffer *b)
{
    if (_buffer_flushed(b)) {
        _buffer_adjust_stack(b);
    }
}


/**
 * @return
 *      The number of written characters.
 */
static size_t
_buffer_append(lulu_Buffer *b, const char *s, size_t n)
{
    // Resulting length would overflow the buffer?
    if (_buffer_len(*b) + n >= _buffer_cap(*b)) {
        _buffer_prep(b);
    }

    // How many indexes left are available?
    size_t rem = _buffer_rem(*b);

    // Clamp size to copy. May be 0.
    size_t to_write = (n > rem) ? rem : n;

    // We assume that `b->buffer` and `s` never alias.
    memcpy(&b->data[b->cursor], s, to_write);
    b->cursor += to_write;
    return to_write;
}


LULU_API void
lulu_write_char(lulu_Buffer *b, char ch)
{
    if (b->cursor >= _buffer_cap(*b)) {
        _buffer_prep(b);
    }
    b->data[b->cursor++] = ch;
}

LULU_API void
lulu_write_string(lulu_Buffer *b, const char *s)
{
    lulu_write_lstring(b, s, strlen(s));
}

LULU_API void
lulu_write_lstring(lulu_Buffer *b, const char *s, size_t n)
{
    // Number of unwritten chars in `s`.
    for (size_t rem = n; rem != 0;) {
        size_t written = _buffer_append(b, s, rem);
        s   += written;
        rem -= written;
    }
}

LULU_API void
lulu_finish_string(lulu_Buffer *b)
{
    _buffer_flushed(b);
    lulu_concat(b->vm, b->pushed);
    b->pushed = 1;
}

LULU_API int
lulu_arg_error(lulu_VM *vm, int argn, const char *whom, const char *msg)
{
    return lulu_errorf(vm, "Bad argument #%i to '%s': %s", argn, whom, msg);
}

LULU_API int
lulu_type_error(lulu_VM *vm, int argn, const char *whom, const char *type_name)
{
    const char *msg = lulu_push_fstring(vm, "'%s' expected, got '%s'",
        type_name, lulu_type_name_at(vm, argn));
    return lulu_arg_error(vm, argn, whom, msg);
}

[[noreturn]]
static void
_type_error(lulu_VM *vm, int argn, const char *whom, lulu_Type tag)
{
    const char *s = lulu_type_name(vm, tag);
    lulu_type_error(vm, argn, whom, s);

#if defined(__GNUC__) || defined(__clang__)
    __builtin_unreachable();
#elif defined(_MSC_VER)
    __assume(false);
#else
#error Please add your compiler's `__builtin_unreachable()` equivalent.
#endif
}

LULU_API void
lulu_check_type(lulu_VM *vm, int argn, lulu_Type type, const char *whom)
{
    if (lulu_type(vm, argn) != type) {
        _type_error(vm, argn, whom, type);
    }
}

LULU_API int
lulu_check_boolean(lulu_VM *vm, int argn, const char *whom)
{
    if (!lulu_is_boolean(vm, argn)) {
        _type_error(vm, argn, whom, LULU_TYPE_BOOLEAN);
    }
    return lulu_to_boolean(vm, argn);
}

LULU_API lulu_Number
lulu_check_number(lulu_VM *vm, int argn, const char *whom)
{
    lulu_Number d = lulu_to_number(vm, argn);
    if (d == 0 && !lulu_is_number(vm, argn)) {
        _type_error(vm, argn, whom, LULU_TYPE_NUMBER);
    }
    return d;
}

LULU_API const char *
lulu_check_lstring(lulu_VM *vm, int argn, size_t *n, const char *whom)
{
    const char *s = lulu_to_lstring(vm, argn, n);
    if (s == nullptr) {
        lulu_type_error(vm, argn, whom, "string");
    }
    return s;
}

LULU_API int
lulu_errorf(lulu_VM *vm, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    lulu_push_vfstring(vm, fmt, args);
    va_end(args);
    return lulu_error(vm);
}

LULU_API void
lulu_set_library(lulu_VM *vm, const char *libname,
    const lulu_Register *library, int n)
{
    if (libname != nullptr) {
        lulu_get_global(vm, libname);
        // _G[libname] doesn't exist yet?
        if (lulu_is_nil(vm, -1)) {
            // Remove the `nil` result from `lulu_get_global()`.
            lulu_pop(vm, 1);

            // Do `_G[libname] = {}`.
            lulu_new_table(vm, 0, n);
            lulu_push_value(vm, -1);
            lulu_set_global(vm, libname);
        }
    }

    for (int i = 0; i < n; i++) {
        // TODO(2025-07-01): Ensure key and value are not collected!
        lulu_push_cfunction(vm, library[i].function);
        lulu_set_field(vm, -2, library[i].name);
    }
}

static const lulu_Register
libs[] = {
    {LULU_BASE_LIB_NAME,   lulu_open_base},
    {LULU_STRING_LIB_NAME, lulu_open_string},
};

LULU_API void
lulu_open_libs(lulu_VM *vm)
{
    for (int i = 0; i < lulu_count_library(libs); i++) {
        lulu_push_cfunction(vm, libs[i].function);
        lulu_push_string(vm, libs[i].name);
        lulu_call(vm, 1, 0);
    }
}
