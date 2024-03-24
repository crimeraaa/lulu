CC = clang
CCFLAGS = -O0 -g -std=c11 -Wall -Wextra -Werror -pedantic
CC_SRC = $(wildcard *.c)
CC_OBJ = $(patsubst %.c, obj/%.o, $(CC_SRC))
CC_EXE = $(patsubst %.c, bin/%, $(CC_SRC))

all: $(CC_EXE)
	
bin obj:
	$(MKDIR) $@

bin/%: obj/%.o | bin
	$(CC) $(CCFLAGS) -o $@ $< -lm
	
obj/%.o: %.c | obj
	$(CC) $(CCFLAGS) -c -o $@ $<
	
clean:
	$(RM) $(CC_OBJ)

uninstall: clean
	$(RM) $(CC_EXE)
	
.PRECIOUS: obj/%.o
	
.PHONY: all clean uninstall
