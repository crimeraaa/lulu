/// See bufio.cpp
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define NEWLINE "\r\n"
#else /// _WIN32 not defined.
#define NEWLINE "\n"
#endif /// _WIN32

#define MAX_BUFFER  32
#define PROMPT      "> "

#define deallocate(T, ptr) reallocate(ptr, sizeof(T), 0)

typedef enum {
    BUFFER_OK,   // We read all that we needed to. We have a full string.
    BUFFER_CONT, // There is still input left, keep going.
    BUFFER_EOF,  // Received the EOF character. Stop everything.
} Buffer_State;

typedef struct {
    char buffer[MAX_BUFFER]; // Contains a portion of input read from `stream`.
    FILE *stream;            // Handle to stream to read from.
    int len;                 // How many slots in `buffer` have been used.
    Buffer_State state;      // Determine course of action for EOF, newlines, etc.
} Buffer_Reader;

typedef struct {
    char *data; // The final, nul-terminated C string.
    int len;    // How many characters there are sans the trailing nul.
} String_Builder;

void *reallocate(void *ptr, size_t size, size_t len) {
    if (len == 0) {
        free(ptr);
        return NULL;
    }
    void *tmp = realloc(ptr, size * len);
    if (tmp == NULL) {
        fprintf(stderr, "%s:%i: Allocation failure. Exiting.\n", __FILE__, __LINE__);
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
        // Might be lone CR, so try to unread the character we just got.
        return ungetc(ch, stream) != EOF;
    }
    return false;
}

void init_reader(Buffer_Reader *self, FILE *stream) {
    memset(self->buffer, 0, sizeof(self->buffer));
    self->stream = stream;
    self->len    = 0;
    self->state  = BUFFER_OK;
}

void read_reader(Buffer_Reader *self) {
    for (int i = 0; i < (int)sizeof(self->buffer); i++) {
        int ch = fgetc(self->stream);
        if (ch == EOF) {
            self->len = i;
            self->state = BUFFER_EOF;
            return;
        } else if (is_newline(ch, self->stream)) {
            self->buffer[i] = '\0';
            self->len = i;
            self->state = BUFFER_OK;
            return;
        }
        self->buffer[i] = ch;
    }
    self->state = BUFFER_CONT;
    self->len = (int)sizeof(self->buffer);
}

void init_builder(String_Builder *self) {
    self->data = NULL;
    self->len  = 0;
}

void free_builder(String_Builder *self) {
    deallocate(char, self->data);
    init_builder(self);
}

void write_builder(String_Builder *self, const char *src, int len) {
    int newlen = self->len + len;
    self->data = reallocate(self->data, sizeof(self->data[1]), newlen + 1);
    
    // Concatenate `src` to `self->data`. Remember that everything to the left
    // of `data` must be preserved!
    for (int i = 0; i < len; i++) {
        self->data[self->len + i] = src[i];
    }
    self->data[newlen] = '\0';
    self->len = newlen;
}

void print_builder(const String_Builder *self) {
    printf("String_Builder := \n"
           "\t{.data := '%s',\n"
           "\t .len  := %i}\n",
           self->data, 
           self->len);    
}

void read_loop() {
    String_Builder bd;
    Buffer_Reader rd;
    
    init_builder(&bd);
    init_reader(&rd, stdin);
    for (;;) {
        // We didn't need to concatenate input so we can safely prompt the user.
        if (rd.state == BUFFER_OK) {
            fputs(PROMPT, stdout);
        }
        read_reader(&rd);
        switch (rd.state) {
        case BUFFER_OK:
            write_builder(&bd, rd.buffer, rd.len);
            printf("'%s'\n(%i chars)\n", bd.data, bd.len);
            free_builder(&bd);
            break;
        case BUFFER_CONT:
            write_builder(&bd, rd.buffer, rd.len);
            break;
        case BUFFER_EOF:    
            free_builder(&bd);
            fputs(NEWLINE, stdout);
            return;
        }
    }
}

int main() {
    read_loop();
    return 0;
}
