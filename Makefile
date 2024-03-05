CC	    := clang
CCFLAGS := -std=c11 -Wall -Wextra -Werror -pedantic

EXE 	:= lulu
CC_SRC	:= $(wildcard *.c)
CC_OBJ	:= $(patsubst %.c, obj/%.o, $(CC_SRC))
DEBUGFLAGS := -fdiagnostics-color=always -g -O0 \
	-DDEBUG_PRINT_CODE -DDEBUG_TRACE_EXECUTION \
	-Wno-unused-function

# -*- BEGIN RECIPES ------------------------------------------------------*- {{{

all: debug

debug: CCFLAGS += $(DEBUGFLAGS)
debug: build

release: CCFLAGS += -Os
release: build

build: bin/$(EXE)

# We need to explicitly link with libm (math library) to have access to `pow()`.
bin/$(EXE): $(CC_OBJ) | bin
	$(CC) $(CCFLAGS) -o $@ $^ -lm

obj/%.o: %.c | obj
	$(CC) $(CCFLAGS) -c -o $@ $<

clean:
	$(RM) $(CC_OBJ)

uninstall: clean
	$(RM) bin/$(EXE)

# }}} -*- END RECIPES --------------------------------------------------------*-

.PHONY: all build debug release clean uninstall
