#include "io.hpp"
#include "mem.hpp"

void init_reader(Reader *r, Reader::Fn fn, void *ctx)
{
    r->readfn  = fn;
    r->context = ctx;
}

void init_buffer(Buffer *b)
{
    b->buffer = NULL;
    b->length = 0;
    b->capacity = 0;
}

void reset_buffer(Buffer *b)
{
    b->length = 0;
}

void resize_buffer(Global *g, Buffer *b, size_t sz)
{
    b->buffer   = resize_pointer(g, b->buffer, b->capacity, sz);
    b->capacity = sz;
}

void free_buffer(Global *g, Buffer *b)
{
    free_pointer(g, b->buffer, b->capacity);
}

void init_stream(Stream *z, Reader::Fn fn, void *ctx)
{
    init_reader(&z->reader, fn, ctx);
    z->unread   = 0;
    z->position = NULL;
}

char fill_stream(Stream *z)
{
    size_t      left;
    const char *buf = z->reader.readfn(&left, z->reader.context);
    // No more to read?
    if (buf == nullptr || left == 0)
        return '\0';

    // Buffer was filled so we can point at it!
    z->unread   = left - 1;
    z->position = buf;
    // Read the very first character, but point to the second one.
    return *z->position++;
}

char getc_stream(Stream *z)
{
    // Still have more to read?
    if (z->unread-- > 0) {
        return *z->position++;
    } else {
        return fill_stream(z);
    }
}

char peek_stream(Stream *z)
{
    if (z->unread == 0) {
        if (fill_stream(z) == '\0') {
            return '\0';
        } else {
            z->unread   += 1; // fill_stream removed first byte, put it back.
            z->position -= 1;
        }
    }
    return z->position[0];
}
