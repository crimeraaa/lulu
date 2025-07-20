#include <string.h>

#include "lulu_auxlib.h"

void
lulu_buffer_init(lulu_VM *vm, lulu_Buffer *b)
{
    b->vm     = vm;
    b->cursor = 0;
    b->pushed = 0;
}

static size_t
_buffer_len(const lulu_Buffer &b)
{
    return b.cursor;
}

static constexpr size_t
_buffer_cap(const lulu_Buffer &b)
{
    return sizeof(b.data);
}

static size_t
_buffer_rem(const lulu_Buffer &b)
{
    return _buffer_cap(b) - _buffer_len(b);
}

[[maybe_unused]]
static const char *
_buffer_end(const lulu_Buffer &b)
{
    return &b.data[0] + sizeof(b.data);
}

static bool
_buffer_flushed(lulu_Buffer *b)
{
    size_t n = b->cursor;
    // Nothing to put on the stack?
    if (n == 0) {
        return false;
    }
    lulu_push_lstring(b->vm, b->data, n);
    b->cursor = 0;
    b->pushed++;
    return true;
}


/**
 * @return
 *      The number of unwritten `char` in `s`.
 */
static size_t
_buffer_append(lulu_Buffer *b, const char *s, size_t n)
{
    // How many indexes left are available?
    size_t rem = _buffer_rem(*b);

    // Clamp size to copy. May be 0.
    size_t to_write = (n > rem) ? rem : n;

    // We assume that `b->buffer` and `s` never alias.
    memcpy(&b->data[b->cursor], s, to_write);
    b->cursor += to_write;
    return n - to_write;
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
        size_t top_len;
        lulu_to_lstring(vm, -1, &top_len);

        // Assumes that since `b->pushed > 1`, we have more strings.
        do {
            size_t here_len;
            lulu_to_lstring(vm, -(to_concat + 1), &here_len);
            if (b->pushed - to_concat + 1 >= LIMIT || top_len > here_len) {
                top_len += here_len;
                to_concat++;
            } else {
                break;
            }
        } while (to_concat < b->pushed);
        lulu_concat(vm, to_concat);
        b->pushed = b->pushed - to_concat + 1;
    }
}

static char *
_buffer_prep(lulu_Buffer *b)
{
    if (_buffer_flushed(b)) {
        _buffer_adjust_stack(b);
    }
    return b->data;
}


void
lulu_buffer_write_char(lulu_Buffer *b, char ch)
{
    if ((b->cursor < _buffer_cap(*b)) || _buffer_prep(b)) {
        b->data[b->cursor++] = ch;
    }
}

void
lulu_buffer_write_string(lulu_Buffer *b, const char *s)
{
    lulu_buffer_write_lstring(b, s, strlen(s));
}

void
lulu_buffer_write_lstring(lulu_Buffer *b, const char *s, size_t n)
{
    for (;;) {
        size_t extra = _buffer_append(b, s, n);
        if (extra == 0) {
            break;
        }
        _buffer_prep(b);
    }
}

void
lulu_buffer_finish(lulu_Buffer *b)
{
    _buffer_flushed(b);
    lulu_concat(b->vm, b->pushed);
    b->pushed = 1;
}

int
lulu_arg_error(lulu_VM *vm, int argn, const char *whom, const char *fmt, ...)
{
    const char *msg = lulu_push_fstring(vm, "Bad argument #%i to '%s'", argn, whom);
    if (fmt != nullptr) {
        va_list args;
        va_start(args, fmt);
        const char *msg2 = lulu_push_vfstring(vm, fmt, args);
        va_end(args);
        return lulu_errorf(vm, "%s: %s", msg, msg2);
    }
    return lulu_error(vm);
}

int
lulu_errorf(lulu_VM *vm, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    lulu_push_vfstring(vm, fmt, args);
    va_end(args);
    return lulu_error(vm);
}

void
lulu_set_library(lulu_VM *vm, const char *libname,
    const lulu_Register *library, int n)
{
    if (libname == nullptr) {
        lulu_push_value(vm, LULU_GLOBALS_INDEX);
    } else {
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
    lulu_pop(vm, 1);
}
