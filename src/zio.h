#ifndef LULU_STREAM_IO_H
#define LULU_STREAM_IO_H

#include "lulu.h"

// Very not robust but should work for simple text inputs and files.
#define LULU_ZIO_EOF                '\0'
#define LULU_ZIO_MINIMUM_BUFFER     32

// Some heap allocated 1D `char` array. Used for building strings.
typedef struct lulu_Buffer {
    char  *buffer;
    size_t length;   // Number of currently written elements, sans nul character.
    size_t capacity; // Number of characters `buffer` is allocated for.
} Buffer;

typedef struct lulu_Stream {
    lulu_VM    *parent;
    lulu_Reader reader;
    void       *context;  // Context to `reader`.
    size_t      unread;
    const char *position;
} Stream;

// lzio.h:luaZ_initbuffer
void luluZIO_init_buffer(Buffer *b);

// lzio.h:luaZ_resetbuffer
void luluZIO_reset_buffer(Buffer *b);

// It is important we do NOT set `b->length` to 0, especially in the case of
// concatenating strings.
// lzio.h:luaZ_resizebuffer
void luluZIO_resize_buffer(lulu_VM *vm, Buffer *b, size_t n);

// lzio.h:luaZ_freebuffer
void luluZIO_free_buffer(lulu_VM *vm, Buffer *b);

// lzio.h:luaZ_init
void luluZIO_init_stream(lulu_VM *vm, Stream *z, lulu_Reader r, void *ctx);

// Calls the reader to get a buffer to poke at. May return nul char.
// lzio.c:luaZ_fill
char luluZIO_fill_stream(Stream *z);

// Returns the current character then advances the stream.
// Incrementing the position pointer and decrementing the number of unread bytes.
// lzio.h:zgetc
char luluZIO_getc_stream(Stream *z);

// Get the character right after the current one, if there is one.
// lzio.c:luaZ_lookahead
char luluZIO_lookahead_stream(Stream *z);

#endif /* LULU_STREAM_IO_H */
