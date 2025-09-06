#include <string.h> // strlen, memcpy

#include "lulu_auxlib.h"


LULU_LIB_API void
lulu_where(lulu_VM *L, int level)
{
    lulu_Debug ar;
    if (lulu_get_stack(L, level, &ar)) {
        lulu_get_info(L, "Sl", &ar);
        if (ar.currentline > 0) {
            lulu_push_fstring(L, "%s:%i: ", ar.source, ar.currentline);
            return;
        }
    }
    // Otherwise, no information available.
    lulu_push_literal(L, "");
}

LULU_LIB_API void
lulu_buffer_init(lulu_VM *L, lulu_Buffer *b)
{
    b->L      = L;
    b->cursor = 0;
    b->pushed = 0;
}


/**
 * @return
 *      The number of characters currently stored in the buffer.
 */
static size_t
buffer_len(const lulu_Buffer *b)
{
    return b->cursor;
}


/**
 * @return
 *      The total number of characters that could be stored in the buffer.
 */
static constexpr size_t
buffer_cap(const lulu_Buffer *b)
{
    return sizeof(b->data);
}


/**
 * @return
 *      The number of free slots in the buffer.
 */
static size_t
buffer_rem(const lulu_Buffer *b)
{
    return buffer_cap(b) - buffer_len(b);
}

static bool
buffer_flushed(lulu_Buffer *b)
{
    size_t n = buffer_len(b);
    // Nothing to put on the stack?
    if (n == 0) {
        return false;
    }
    lulu_push_lstring(b->L, b->data, n);
    b->cursor = 0;
    b->pushed++;
    return true;
}


static constexpr int LIMIT = LULU_STACK_MIN / 2;


/** @brief Concatenate temporary strings to avoid VM stack overflow. */
static void
buffer_adjust_stack(lulu_Buffer *b)
{
    // More than 1 string previously pushed, so we need to manage them.
    if (b->pushed > 1) {
        lulu_VM *L        = b->L;
        int      to_concat = 1; // Number of levels to concatenate.

        // Accumulator length for all the temporaries we will concatenate.
        // Starts with the one currently on the top of the stack.
        size_t acc_len = lulu_obj_len(L, -1);

        // Assumes that since `b->pushed > 1`, we have at least 2 strings.
        do {
            size_t here_len = lulu_obj_len(L, -(to_concat + 1));

            // We have too many strings OR our total length exceeds this one?
            if (b->pushed - to_concat + 1 >= LIMIT || acc_len > here_len) {
                acc_len += here_len;
                to_concat++;
            } else {
                break;
            }
        } while (to_concat < b->pushed);
        lulu_concat(L, to_concat);
        b->pushed = b->pushed - to_concat + 1;
    }
}

static void
buffer_prep(lulu_Buffer *b)
{
    if (buffer_flushed(b)) {
        buffer_adjust_stack(b);
    }
}


/**
 * @return
 *      The number of written characters.
 */
static size_t
buffer_append(lulu_Buffer *b, const char *s, size_t n)
{
    // Resulting length would overflow the buffer?
    if (buffer_len(b) + n >= buffer_cap(b)) {
        buffer_prep(b);
    }

    // How many indexes left are available?
    size_t rem = buffer_rem(b);

    // Clamp size to copy. May be 0.
    size_t to_write = (n > rem) ? rem : n;

    // We assume that `b->buffer` and `s` never alias.
    memcpy(&b->data[b->cursor], s, to_write);
    b->cursor += to_write;
    return to_write;
}


LULU_LIB_API void
lulu_write_char(lulu_Buffer *b, char ch)
{
    if (b->cursor >= buffer_cap(b)) {
        buffer_prep(b);
    }
    b->data[b->cursor++] = ch;
}

LULU_LIB_API void
lulu_write_string(lulu_Buffer *b, const char *s)
{
    lulu_write_lstring(b, s, strlen(s));
}

LULU_LIB_API void
lulu_write_lstring(lulu_Buffer *b, const char *s, size_t n)
{
    // Number of unwritten chars in `s`.
    for (size_t rem = n; rem != 0;) {
        size_t written = buffer_append(b, s, rem);
        s += written;
        rem -= written;
    }
}

LULU_LIB_API void
lulu_finish_string(lulu_Buffer *b)
{
    buffer_flushed(b);
    lulu_concat(b->L, b->pushed);
    b->pushed = 1;
}

static int
resolve_index(lulu_VM *L, lulu_Debug *ar, int argn, const char **what)
{
    if (argn > 0) {
        return argn;
    }

    /* Less than 0, but is a valid relative stack index? */
    if (argn > LULU_PSEUDO_INDEX) {
        return lulu_get_top(L) + argn;
    }

    /* Is a valid upvalue index? */
    if (lulu_upvalue_index(ar->nups) <= argn && argn <= lulu_upvalue_index(1)) {
        argn -= lulu_upvalue_index(0);
        *what = "upvalue";
    } else {
        argn -= LULU_PSEUDO_INDEX;
        *what = "pseudo-index";
    }
    return -argn;
}

LULU_LIB_API int
lulu_arg_error(lulu_VM *L, int argn, const char *msg)
{
    lulu_Debug ar;
    const char *what = "argument";
    // Use level 0 because this function is only ever called by C functions.
    if (!lulu_get_stack(L, 0, &ar)) {
        return lulu_errorf(L, "Bad %s #%i (%s)", what, argn, msg);
    }
    lulu_get_info(L, "nu", &ar);
    argn = resolve_index(L, &ar, argn, &what);
    return lulu_errorf(L, "Bad %s #%i to '%s' (%s)", what, argn, ar.name, msg);
}

LULU_LIB_API int
lulu_type_error(lulu_VM *L, int argn, const char *type_name)
{
    const char *msg = lulu_push_fstring(L, "'%s' expected, got '%s'",
        type_name, lulu_type_name_at(L, argn));
    return lulu_arg_error(L, argn, msg);
}

[[noreturn]] static void
type_error(lulu_VM *L, int argn, lulu_Type tag)
{
    const char *s = lulu_type_name(L, tag);
    lulu_type_error(L, argn, s);

#if defined(__GNUC__) || defined(__clang__)
    __builtin_unreachable();
#elif defined(_MSC_VER)
    __assume(false);
#else
#    error Please add your compiler's `__builtin_unreachable()` equivalent.
#endif
}

LULU_LIB_API void
lulu_check_any(lulu_VM *L, int argn)
{
    if (lulu_is_none(L, argn)) {
        lulu_arg_error(L, argn, "value expected");
    }
}

LULU_LIB_API void
lulu_check_type(lulu_VM *L, int argn, lulu_Type type)
{
    if (lulu_type(L, argn) != type) {
        type_error(L, argn, type);
    }
}

LULU_LIB_API int
lulu_check_boolean(lulu_VM *L, int argn)
{
    if (!lulu_is_boolean(L, argn)) {
        type_error(L, argn, LULU_TYPE_BOOLEAN);
    }
    return lulu_to_boolean(L, argn);
}

LULU_LIB_API lulu_Number
lulu_check_number(lulu_VM *L, int argn)
{
    lulu_Number d = lulu_to_number(L, argn);
    if (d == 0 && !lulu_is_number(L, argn)) {
        type_error(L, argn, LULU_TYPE_NUMBER);
    }
    return d;
}

LULU_LIB_API lulu_Integer
lulu_check_integer(lulu_VM *L, int argn)
{
    lulu_Integer i = lulu_to_integer(L, argn);
    if (i == 0 && !lulu_is_number(L, argn)) {
        type_error(L, argn, LULU_TYPE_NUMBER);
    }
    return i;
}

LULU_LIB_API const char *
lulu_check_lstring(lulu_VM *L, int argn, size_t *n)
{
    const char *s = lulu_to_lstring(L, argn, n);
    if (s == nullptr) {
        type_error(L, argn, LULU_TYPE_STRING);
    }
    return s;
}

LULU_LIB_API void *
lulu_check_userdata(lulu_VM *L, int argn, const char *mt_name)
{
    void *p = lulu_to_userdata(L, argn);
    /* Value is a userdata? */
    if (p != nullptr) {
        /* Userdata has a metatable? (full userdata only) */
        if (lulu_get_metatable(L, argn)) {
            /* Get library metatable */
            lulu_get_field(L, LULU_REGISTRY_INDEX, mt_name);
            /* Userdata's metatable is the same as the library metatable? */
            if (lulu_equal(L, -2, -1)) {
                /* Remove the library metatables from the stack. */
                lulu_pop(L, 2);
                return p;
            }
        }
    }
    lulu_type_error(L, argn, mt_name);
    return nullptr;
}

LULU_LIB_API lulu_Number
lulu_opt_number(lulu_VM *L, int argn, lulu_Number def)
{
    if (lulu_is_none_or_nil(L, argn)) {
        return def;
    }
    return lulu_check_number(L, argn);
}

LULU_LIB_API lulu_Integer
lulu_opt_integer(lulu_VM *L, int argn, lulu_Integer def)
{
    if (lulu_is_none_or_nil(L, argn)) {
        return def;
    }
    return lulu_check_integer(L, argn);
}

LULU_LIB_API const char *
lulu_opt_lstring(lulu_VM *L, int argn, const char *def, size_t *n)
{
    if (lulu_is_none_or_nil(L, argn)) {
        if (n != nullptr) {
            *n = (def != nullptr) ? strlen(def) : 0;
        }
        return def;
    }
    return lulu_check_lstring(L, argn, n);
}

LULU_LIB_API int LULU_ATTR_PRINTF(2, 3)
lulu_errorf(lulu_VM *L, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    lulu_where(L, 1);
    lulu_push_vfstring(L, fmt, args);
    va_end(args);
    lulu_concat(L, 2);
    return lulu_error(L);
}

LULU_LIB_API void
lulu_set_nlibrary(lulu_VM *L, const char *libname,
    const lulu_Register *library, int n)
{
    if (libname != nullptr) {
        lulu_get_global(L, libname);
        // _G[libname] doesn't exist yet?
        if (lulu_is_nil(L, -1)) {
            // Remove the `nil` result from `lulu_get_global()`.
            lulu_pop(L, 1);

            // Do `_G[libname] = {}`.
            lulu_new_table(L, n, 0);
            lulu_push_value(L, -1);
            lulu_set_global(L, libname);
        }
    }

    for (int i = 0; i < n; i++) {
        lulu_push_cfunction(L, library[i].function);
        lulu_set_field(L, -2, library[i].name);
    }
}

LULU_LIB_API int
lulu_new_metatable(lulu_VM *L, const char *mt_name)
{
    lulu_get_field(L, LULU_REGISTRY_INDEX, mt_name);
    /* Table was pushed, meaning it's already in use? */
    if (!lulu_is_nil(L, -1)) {
        return 0;
    }
    /* Remove nil */
    lulu_pop(L, 1);
    lulu_new_table(L, 0, 0); /* mt */
    lulu_push_value(L, -1);  /* mt, mt */

    /* mt ; registry[mt_name] = mt */
    lulu_set_field(L, LULU_REGISTRY_INDEX, mt_name);
    return 1;
}

[[maybe_unused]]
static const lulu_Register libs[] = {
    {LULU_BASE_LIB_NAME, lulu_open_base},
    {LULU_MATH_LIB_NAME, lulu_open_math},
    {LULU_STRING_LIB_NAME, lulu_open_string},
    {LULU_OS_LIB_NAME, lulu_open_os},
    {LULU_IO_LIB_NAME, lulu_open_io},
};

LULU_LIB_API void
lulu_open_libs(lulu_VM *L)
{
#ifdef LULU_DEBUG_STRESS_GC
    libs[0].function(L);
#else
    for (int i = 0; i < lulu_count_library(libs); i++) {
        lulu_push_cfunction(L, libs[i].function);
        lulu_push_string(L, libs[i].name);
        lulu_call(L, 1, 0);
    }
#endif
}
