/// See bufio.cpp
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ansi.h"

// No need for this as Windows' stdout automatically converts \n to \r\n
// Although the same cannot be said for text files obviously...
// #define NEWLINE "\n"

#define MAX_BUFFER  256
#define PROMPT      "> "

#define arraylen(array)         (sizeof(array) / sizeof(array[0]))
#define deallocate(T, ptr)      reallocate(ptr, sizeof(T), 0)
#define resize_array(T, ptr, len)   reallocate(ptr, sizeof(T), len)

#define _stringify(x)           #x
#define stringify(x)            _stringify(x)
#define loglocation()           __FILE__ ":" stringify(__LINE__)
#define logformat(info)         loglocation() ": " info
#define logprintln(info)        fputs(logformat(info) "\n", stderr)
#define logprintf(fmt, ...)     fprintf(stderr, logformat(fmt), __VA_ARGS__)
#define logprintfln(fmt, ...)   logprintf(fmt "\n", __VA_ARGS__)

typedef enum {
    BUF_OK,   // We read all that we needed to. We have a full string.
    BUF_CONT, // There is still input left, keep going.
    BUF_EOF,  // Received the EOF character. Stop everything.
    BUF_ERR,  // `ferror()` returned non-zero.
} Buffer_State;

typedef struct {
    char buffer[MAX_BUFFER]; // Contains a portion of input read from `stream`.
    char *end;               // Pointer to 1 past the last element of `buffer`.
    FILE *stream;            // Handle to stream to read from.
    Buffer_State state;      // Determine course of action for EOF, newlines, etc.
} BufIO_Reader;

typedef struct {
    char *data; // A heap-allocated nul-terminated C string.
    char *end;  // Pointer to 1 past last element of `data`.
    int len;    // How many characters there are sans the trailing nul.
    int prev;   // Character to the left of the one we're current writing.
} String_Builder;

void *reallocate(void *ptr, size_t size, size_t len) {
    if (len == 0) {
        free(ptr);
        return NULL;
    }
    void *tmp = realloc(ptr, size * len);
    if (tmp == NULL) {
        logprintln("[ERROR]: Allocation failure, aborting program.");
        exit(EXIT_FAILURE);
    }
    return tmp;
}

static bool is_newline(int ch, FILE *stream) {
    // If is lone LF, can assume it's good.
    if (ch == '\n') {
        return true;
    }
    // Have CR, read next character and only proceed if non-EOF.
    if (ch == '\r' && (ch = fgetc(stream)) != EOF) {
        // Is LF which means this is likely a CRLF.
        if (ch == '\n') {
            return true;
        }
        // Lone CR line ending is only on Commodore 64 and pre-OSX Macs so I
        // really don't want to bother with them.
        if (ungetc(ch, stream) == EOF) {
            return false;
        }
    }
    return false;
}

void init_bufioreader(BufIO_Reader *self, FILE *stream, Buffer_State start) {
    memset(self->buffer, 0, sizeof(self->buffer));
    self->stream = stream;
    self->end    = self->buffer;
    self->state  = start;
}

// Read the contents of `self->stream` into the buffer.
// This will likely truncate as we don't care about line endings.
void readfile_bufioreader(BufIO_Reader *self) {
    FILE *stream = self->stream;
    size_t sz = fread(self->buffer,
                      sizeof(self->buffer[0]),
                      arraylen(self->buffer),
                      stream);
    self->end = self->buffer + sz;

    // We have no concept of "compeleted" until we reach EOF. We either have
    // more things to read or we're done.
    if (ferror(stream)) {
        self->state = BUF_ERR;
    } else if (feof(stream)) {
        self->state = BUF_EOF;
    } else {
        self->state = BUF_CONT;
    }
}

// Repeatedly calls `fgetc` on `self->stream` until we hit EOF, a newline, or we
// exhausted all slots in the internal buffer.
void readline_bufioreader(BufIO_Reader *self) {
    FILE *stream = self->stream;

    // Reset to point at start.
    self->end = self->buffer;
    for (int i = 0; i < (int)sizeof(self->buffer); i++) {
        int ch = fgetc(stream);
        if (ch == EOF) {
            self->state = ferror(stream) ? BUF_ERR : BUF_EOF;
            return;
        } else if (is_newline(ch, stream)) {
            *self->end = '\0';
            self->state = BUF_OK;
            return;
        }
        *self->end = ch;
        self->end++;
    }
    self->state = BUF_CONT;
    self->end   = self->buffer + arraylen(self->buffer);
}

void init_stringbuilder(String_Builder *self) {
    self->data = NULL;
    self->end  = NULL;
    self->len  = 0;
    self->prev = 0;
}

void free_stringbuilder(String_Builder *self) {
    deallocate(char, self->data);
    init_stringbuilder(self);
}

static int get_escape(int ch) {
    switch (ch) {
    case 'a':  return '\a'; // Bell
    case 'b':  return '\b'; // Backspace
    // case 'e':  return '\e'; // Non-standard
    case 'f':  return '\f'; // Form Feed
    case 'n':  return '\n'; // Line Feed
    case 'r':  return '\r'; // Carriage Return
    case 't':  return '\t'; // Horizontal Tab
    case 'v':  return '\v'; // Vertical Tab
    case '\\': return '\\';
    case '\"': return '\"';
    case '\'': return '\'';
    default:   return ch;   // TODO: Handle hex constants properly?
    }
}

/**
 * @brief   Append character `ch` to the heap-allocated `data`.
 *          If the previously read character was an escape character, we need to
 *          handle that appropriately.
 * @note    See: https://en.wikipedia.org/wiki/Escape_sequences_in_C
 */
void append_stringbuilder(String_Builder *self, int ch) {
    if (self->prev == '\\') {
        *self->end = get_escape(ch);
        self->prev = 0; // Unset so this returns false next time
    } else {
        *self->end = ch;
    }
    self->end++;
}

void truncate_stringbuilder(String_Builder *self, int len) {
    self->data[len] = '\0';
    self->len       = len;
}

// Append nul-terminated C string of known length to a String_Builder instance.
void concat_stringbuilder(String_Builder *self, const char *src, int len) {
    int newlen = self->len + len;
    int escapes = 0; // How many escaped characters do we need to account for?

    self->data = resize_array(char, self->data, newlen + 1);
    self->end  = self->data + self->len;

    // Concatenate `src` to `self->data`. Remember that everything to the left
    // of `data` must be preserved!
    for (int i = 0; i < len; i++) {
        int ch = src[i];
        /* Save state, especially if an escape sequence was truncated. We do not
        append the '\\' character itself.

        Also note that if we previously escaped a '\\', we shouldn't escape the
        current one for strings like "hello\\nthere". */
        if (ch == '\\' && self->prev != '\\') {
            escapes++;
            self->prev = ch;
            continue;
        }
        append_stringbuilder(self, ch);
    }
    truncate_stringbuilder(self, newlen - escapes);
}

#define ANCHOR_BEGINNING    set_color(SGR_RED) "^" reset_colors()
#define ANCHOR_ENDING       set_color(SGR_RED) "$" reset_colors()

// `buffer` may not necessarily be nul-terminated.
void print_buffer(const char *buffer, int len) {
    printf("[BUFFER]: %i chars\n", len);
    printf(ANCHOR_BEGINNING);
    printf("%.*s", len, buffer);
    printf(ANCHOR_ENDING "\n");
}

void try_file(const char *filename) {
    FILE *handle = fopen(filename, "r");
    BufIO_Reader reader;

    if (handle == NULL) {
        perror(logformat("Failed to open file."));
        return;
    }

    init_bufioreader(&reader, handle, BUF_CONT);
    for (;;) {
        readfile_bufioreader(&reader);
        int len = (int)(reader.end - reader.buffer);
        switch (reader.state) {
        case BUF_OK: // Should not happen
        case BUF_CONT:
            print_buffer(reader.buffer, len);
            break;
        case BUF_ERR:
            logprintfln("[ERROR]: ferror() != 0 while reading file '%s'.", filename);
        case BUF_EOF:
            print_buffer(reader.buffer, len);
            fclose(handle);
            return;
        }
    }
}

void try_repl() {
    String_Builder builder;
    BufIO_Reader reader;

    init_stringbuilder(&builder);
    init_bufioreader(&reader, stdin, BUF_OK);
    for (;;) {
        // We didn't need to concatenate input so we can safely prompt the user.
        if (reader.state == BUF_OK) {
            fputs(PROMPT, stdout);
        }
        readline_bufioreader(&reader);
        int len = (int)(reader.end - reader.buffer);
        switch (reader.state) {
        case BUF_OK:
            concat_stringbuilder(&builder, reader.buffer, len);
            print_buffer(builder.data, builder.len);
            free_stringbuilder(&builder);
            break;
        case BUF_CONT:
            concat_stringbuilder(&builder, reader.buffer, len);
            break;
        case BUF_ERR:
            logprintln("[ERROR]: ferror() != 0 while reading stdin.");
            // Fall through
        case BUF_EOF:
            free_stringbuilder(&builder);
            fputc('\n', stdout);
            return;
        }
    }
}

int main(int argc, const char *argv[]) {
    if (argc == 1) {
        try_repl();
    } else if (argc == 2) {
        try_file(argv[1]);
    } else {
        fprintf(stderr, "Usage: %s [file]\n", argv[0]);
        return 1;
    }
    return 0;
}
