#ifdef LULU_DEBUG

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif // _GNU_SOURCE

#include <execinfo.h> // backtrace, backtrace_symbols_fd
#include <unistd.h>   // STD*_FILENO, write
#include <stdio.h>    // fprintf, sscanf
#include <stdlib.h>   // free
#include <string.h>   // strlen
#include <assert.h>   // assert

#include <dlfcn.h>          // dl{open,close,sym,error}
#include <gnu/lib-names.h>

#include "private.hpp"
#include "slice.hpp"

#undef lulu_assert

/**
 * @brief
 *  -   Parses a string which was returned from `backtrace_symbols()` to get
 *      symbol name and the offset.
 */
static void
parse_strings(String stack_frame, Slice<char> out_symbol, Slice<char> out_offset)
{
    String symbol{nullptr, 0};
    String offset{nullptr, 0};
    
    for (const auto &ch : stack_frame) {
        switch (ch) {
        // Beginning of symbol?
        case '(':
            symbol.data = &ch + 1;
            break;
        // Beginning of offset?
        case '+':
            symbol.len  = cast(size_t, &ch - symbol.data);
            offset.data = &ch + 1;
            break;
        // End of offset?
        case ')':
            offset.len = cast(size_t, &ch - offset.data);
        default:
            break;
        }
    }
    
    copy(out_symbol, symbol);
    copy(out_offset, offset);
}

static void
print_dlerror()
{
    const char *errmsg = dlerror();
    write(STDERR_FILENO, errmsg, strlen(errmsg));
}

static void *
calculate_offset(String stack_frame)
{
    char _buf1[75] = {0};
    char _buf2[25] = {0};
    Slice<char> symbol_string{_buf1, count_of(_buf1)};
    Slice<char> offset_string{_buf2, count_of(_buf2)};
    
    // Parse the string obtained by `backtrace_symbols()` to get the symbol and
    // offset.
    parse_strings(stack_frame, symbol_string, offset_string);
    
    // Convert the offset from a string to a pointer.
    void *offset_pointer;
    int status_sscanf = sscanf(raw_data(offset_string), "%p", &offset_pointer);
    
    // Check if a symbol string was created. If yes, convert symbol string to
    // an offset.
    if (symbol_string[0] != '\0') {
        void *object_file = dlopen(NULL, RTLD_LAZY);
        if (object_file == nullptr) {
            print_dlerror();
        }

        // Convert the symbol string to an address.
        void *address = dlsym(object_file, raw_data(symbol_string));
        if (address == nullptr) {
            print_dlerror();
        }
        // Extract the symbolic information pointed to by `address`.
        Dl_info symbol_info;
        if (dladdr(address, &symbol_info) != 0) {
            // Caculate total offset oof the symbol
            char *saddr = cast(char *, symbol_info.dli_saddr);
            char *fbase = cast(char *, symbol_info.dli_fbase);
            char *ofptr = cast(char *, offset_pointer);

            offset_pointer  = cast(void *, (saddr - fbase) + ofptr);
            dlclose(object_file);
        } else {
            print_dlerror();
        }
    }
    
    return (status_sscanf != EOF) ? offset_pointer : nullptr;
}


// J. Panek originally wrote 128 but come on, do we need THAT many?
#define STACK_FRAMES_BUFFER_SIZE    16

static void *stack_frames_buffer[STACK_FRAMES_BUFFER_SIZE];
static char  execution_filename[32] = "bin/lulu";

static void
addr2line_print(const void *address)
{
    char command[512] = {0};

    /**
     * @brief
     *  -   Ensure our command maps to the relevant lines in the code.
     *
     * @note 2025-06-11
     *  -C  means demangle.
     *  -i  means to print if a a particular function call was inlined.
     *  -f  means to print function name and line number information.
     *  -p  means to print the function name, file name, and line number.
     *  -s  means to print only the base of each file name.
     *  -a  means to print addresses before all other information.
     *  -e  uses the following positional argument as the name of the
     *      executable to translate addresses from.
     */
    sprintf(command, "addr2line -C -i -f -p -s -a -e ./%s %p ",
        execution_filename, address);

    // Will print a nicely formatted string specifying the function and source
    // line of the address.
    system(command);
}

/**
 * @link
 *  - https://stackoverflow.com/a/55511761
 */
static void
print_backtrace()
{
    const char errmsg[]   = "Offset cannot be resolved; No offset present?\n\0?";
    char print_array[100] = {0};

    // backtrace the last calls
    int    n     = backtrace(stack_frames_buffer, count_of(stack_frames_buffer));
    char **array = backtrace_symbols(stack_frames_buffer, n);

    Slice<char *> stack_frame_strings{array, cast(size_t, n)};
    
    sprintf(print_array, "\nObtained %i stack frames.\n", n);
    write(STDERR_FILENO, print_array, strlen(print_array));
    
    for (const char *s : stack_frame_strings) {
#if __x86_64__
        // Calculate the offset on x86_64, print the file and line number with
        // addr2line.
        void *offset_pointer = calculate_offset(string_make(s));
        if (offset_pointer == nullptr) {
            write(STDERR_FILENO, errmsg, count_of(errmsg));
        } else {
            addr2line_print(offset_pointer);
        }
#elif __arm__
#error nah
#endif
    }
    free(array);
}

void
lulu_assert(const char *file, int line, bool cond, const char *expr)
{
    static bool have_error = false;
    if (!cond) {
        assert(!have_error && "Error in assertion handling");
        have_error = true;
        fprintf(stderr, "%s:%i: Assertion failed: %s\n", file, line, expr);
        print_backtrace();
        __builtin_trap();
    }
}

#endif // LULU_DEBUG
