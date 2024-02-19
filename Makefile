CC	    := clang
CCFLAGS := -std=c11 -Wall -Wextra -Werror -pedantic \
	-Wno-unused-variable -Wno-unused-parameter -Wno-unused-function

EXE 	:= lulu
CC_SRC	:= $(wildcard *.c)
CC_OBJ	:= $(patsubst %.c, obj/%.o, $(CC_SRC))

all: debug
	
debug: CCFLAGS += -O0 -g -DDEBUG_PRINT_CODE
debug: build

release: CCFLAGS += -O2 -s
debug: build

build: bin/$(EXE)

bin/$(EXE): $(CC_OBJ) | bin
	$(CC) $(CCFLAGS) -o $@ $^

obj/%.o: %.c | obj
	$(CC) $(CCFLAGS) -c -o $@ $<

clean:
	$(RM) $(CC_OBJ)

uninstall: clean
	$(RM) bin/$(EXE)
	
.PHONY: all build debug release clean uninstall
