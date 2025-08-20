#pragma once

#include "private.hpp"

#define STREAM_END      -1

struct Stream {
    lulu_Reader function;
    void *data;

    // Pointer to current position in buffer stored at `data`.
    const char *cursor;

    // How many bytes are still unread by the reader function?
    // Useful when implementing file buffering.
    isize remaining;

    int
    fill()
    {
        usize n;
        const char *s = this->function(this->data, &n);
        if (s == nullptr || n == 0) {
            return STREAM_END;
        }

        // - 1 because we read the first character then move past it.
        this->remaining = n - 1;
        this->cursor    = s;
        return *this->cursor++;
    }

    int
    get_char()
    {
        // Note that we are comparing the value BEFORE the decrement.
        if (this->remaining-- > 0) {
            return *this->cursor++;
        }
        return this->fill();
    }
};
