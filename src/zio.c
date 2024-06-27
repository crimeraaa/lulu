#include "zio.h"
#include "memory.h"

void luluZIO_init_buffer(Buffer *b)
{
    b->buffer   = NULL;
    b->capacity = 0;
    luluZIO_reset_buffer(b);
}

void luluZIO_reset_buffer(Buffer *b)
{
    b->length = 0;
}

void luluZIO_resize_buffer(lulu_VM *vm, Buffer *b, size_t n)
{
    b->buffer   = luluMem_resize_parray(vm, b->buffer, b->capacity, n);
    b->capacity = n;
}

void luluZIO_free_buffer(lulu_VM *vm, Buffer *b)
{
    luluMem_free_parray(vm, b->buffer, b->capacity);
    luluZIO_init_buffer(b);
}

void luluZIO_init_stream(lulu_VM *vm, Stream *z, lulu_Reader r, void *ctx)
{
    z->parent   = vm;
    z->reader   = r;
    z->context  = ctx;
    z->unread   = 0;
    z->position = NULL;
}

char luluZIO_fill_stream(Stream *z)
{
    size_t      sz;
    const char *buf = z->reader(z->parent, &sz, z->context);
    // No more to read?
    if (buf == NULL || sz == 0)
        return LULU_ZIO_EOF;
    z->unread   = sz;
    z->position = buf;
    return z->position[0];
}

char luluZIO_getc_stream(Stream *z)
{
    if (z->unread == 0)
        luluZIO_fill_stream(z);
    z->unread   -= 1;
    z->position += 1;
    return z->position[-1];
}

char luluZIO_lookahead_stream(Stream *z)
{
    if (z->unread == 0) {
        if (luluZIO_fill_stream(z) == LULU_ZIO_EOF)
            return LULU_ZIO_EOF;
    }
    return z->position[0];
}

