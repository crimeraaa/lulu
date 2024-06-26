#pragma once

#include "conf.hpp"

struct Reader {
    using Fn = const char *(*)(size_t *out, void *ctx);
    Fn    readfn;  // Callback function.
    void *context; // C-style pseudo-closures.
};

struct Buffer {
    char  *buffer;
    size_t length;
    size_t capacity;
};

struct Stream {
    Reader      reader; // Has its own context.
    size_t      unread;
    const char *position;
};

void init_reader(Reader *r, Reader::Fn fn, void *ctx);

// lzio.c:luaZ_initbuffer
void init_buffer(Buffer *b);

// Returns a read-only pointer to the desired position in the buffer.
// If `offset` is negative, we will get a pointer relative to the end.
// e.g. `-1` will be the very last character.
// If `offset` is positive, we will get an absolute index.
//
// lzio.h:luaZ_buffer
const char *view_buffer(Buffer *b, int offset = 0);

// Currently allocated capacity for the buffer.
// lzio.h:luaZ_sizebuffer
size_t size_buffer(Buffer *b);

// Number of actively used elements, however you choose to enforce that.
// lzio.h:luaZ_lenbuffer
size_t length_buffer(Buffer *b);

// lzio.h:luaZ_resetbuffer
void reset_buffer(Buffer *b);

// lzio.c:luaZ_resizebuffer
void resize_buffer(Global *g, Buffer *b, size_t sz);

// lzio.c:luaZ_freebuffer
void free_buffer(Global *g, Buffer *b);

// lzio.c:luaZ_init
void init_stream(Stream *z, Reader::Fn fn, void *ctx);

// Updates the internal reader and attempts to get a view into it.
// lzio.c:luaZ_fill
char fill_stream(Stream *z);

// Returns currently viewed character and advances the internal view if possible.
// lzio.h:zgetc
char getc_stream(Stream *z);
void ungetc_stream(Stream *z);

// Retrieves lookahead character if there is one.
// lzio.c:luaZ_lookahead
char peek_stream(Stream *z);
