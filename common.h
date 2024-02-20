#ifndef LUA_COMMON_H
#define LUA_COMMON_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define xtostring(Macro)        #Macro
#define stringify(Macro)        xtostring(Macro)
#define logstring(Message)      __FILE__ ":" stringify(__LINE__) ": " Message
#define logprintln(Message)     fputs(logstring(Message) "\n", stderr)
#define logprintf(Format, ...)  fprintf(stderr, logstring(Format), __VA_ARGS__)

#endif /* LUA_COMMON_H */
