#pragma once

#include "private.hpp"

#define STREAM_END      -1

struct LULU_PRIVATE Stream {
    lulu_Reader function;
    void       *data;
    const char *cursor;     // Current position in buffer somewhere at `data.`
    isize       remaining;  // How many bytes still unread?

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
