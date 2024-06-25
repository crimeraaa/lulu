/**
 * @see https://www.lua.org/source/5.1/lzio.h.html#ZIO
 */
#ifndef LULU_BUFFER_H
#define LULU_BUFFER_H

#include "lulu.h"
#include "limits.h"

#define LULU_EOF    EOF

typedef struct lulu_Reader {
    lulu_ReadFn read;
    void       *context;
} Reader;

// A growable, heap-allocated character array.
typedef struct lulu_Buffer {
    char  *buffer;
    size_t length;
    size_t capacity;
} Buffer;

// Buffered streams.
typedef struct lulu_Stream {
    Reader      reader;
    const char *position; // current position in buffer.
    size_t      unread;   // number of unread bytes.
    lulu_VM    *vm;       // To be passed to `reader`.
} Stream;

#endif /* LULU_BUFFER_H */
