#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#define NEWLINE     "\r\n"
#else
#define NEWLINE     "\n"
#endif

#define MAX_BUFFER  32
#define PROMPT      "> "

static bool is_newline(int ch) {
    // Have lone LF so must be good.
    if (ch == '\n') {
        return true;
    }

    // Have CR, check the next character to see if it's LF.
    if (ch == '\r') {
        ch = fgetc(stdin);

        // Is actually a CRLF?
        if (ch == '\n') {
            return true;
        } 

        // Was lone CR so try to return whatever that was.
        return ungetc(ch, stdin) != EOF;
    } 
    return false;
}

enum class BufState : int {
    Complete,  // Input was fully consumed, no truncation needed.
    Truncated, // Input could not fit the buffer so it had to be truncated.
    StreamEnd, // No more input, or CTRL-D (*Nix) or CTRL-Z + ENTER (Win).
};

static BufState fill_buffer(char* buffer, int total, int& len) {
    for (int i = 0; i < total; i++) {
        int ch = fgetc(stdin);
        if (ch == EOF) {
            len = i;
            return BufState::StreamEnd;
        }
        if (is_newline(ch)) {
            buffer[i] = '\0';
            len = i;
            return BufState::Complete;
        }
        buffer[i] = ch;
    }
    len = total;
    return BufState::Truncated;
}

template<class T>
T* reallocate(T* ptr, size_t len) {
    void* src = static_cast<void*>(ptr);
    if (len == 0) {
        free(src);
        return NULL;
    } 
    void* tmp = realloc(src, sizeof(T) * len);
    if (tmp == nullptr) {
        fprintf(stderr, "%s:%i: Allocation failure, exiting.\n", __FILE__, __LINE__);
        exit(EXIT_FAILURE);
    }
    return static_cast<T*>(tmp);
}

template<class T>
void deallocate(T* ptr) {
    reallocate(ptr, 0);
}

struct Builder {
    char* data;
    int len;    // Number of non-nul chars, or the index of the nul char itself.
};

void init_builder(Builder& self, int len = 0) {
    self.data = nullptr;
    self.len = len;
}

void free_builder(Builder& self) {
    deallocate(self.data);
    init_builder(self);
}

// Concatenate a C string to the builder.
void write_builder(Builder& self, const char *src, int len) {
    int newlen = self.len + len;
    self.data = reallocate(self.data, newlen + 1);
    for (int i = 0; i < len; i++) {
        self.data[self.len + i] = src[i];
    }
    self.data[newlen] = '\0';
    self.len = newlen;
}

void print_builder(const Builder& self) {
    printf("Builder := {\n"
           "\t.data := '%s'\n"
           "\t.len  := %i\n"
           "}\n",
           self.data, 
           self.len);    
}

void read_loop() {
    char buffer[MAX_BUFFER];
    int len;
    Builder bldr;
    BufState state = BufState::Complete;
    
    init_builder(bldr);
    while (true) {
        if (state == BufState::Complete) {
            fputs(PROMPT, stdout);
        }
        // This call will populate `len`.
        state = fill_buffer(buffer, sizeof(buffer), len);
        switch (state) {
        case BufState::Complete:
            write_builder(bldr, buffer, len);
            print_builder(bldr);
            free_builder(bldr);
            break;
        case BufState::Truncated:
            write_builder(bldr, buffer, len);
            break;
        case BufState::StreamEnd:
            fputs(NEWLINE, stdout);
            free_builder(bldr);
            return;
        }
    }
}

int main() {
    read_loop();
    return 0;
}
