CC := clang
CC_FLAGS := -std=c11 -Wall -Wextra -Werror -pedantic -fdiagnostics-color=always \
			-Wno-error=unused-variable -Wno-error=unused-parameter \
			-Wno-error=unused-function

CC_SRC := $(wildcard *.c)
CC_OBJ := $(patsubst %.c,obj/%.o,$(CC_SRC))
CC_EXE := $(patsubst %.c,bin/%,$(CC_SRC))

all: debug
	
debug: CC_FLAGS += -O0 -g
debug: build

release: CC_FLAGS += -Os
release: build
	
dump:
	@echo C Compiler: $(CC)
	@echo C Flags: $(CC_FLAGS)
	@echo C Sources: $(CC_SRC)
	@echo C Objects: $(CC_OBJ)
	@echo C Outputs: $(CC_EXE)
	
build: $(CC_EXE)
	
bin obj:
	$(MKDIR) $@

bin/%: obj/%.o | bin
	$(CC) $(CC_FLAGS) -o $@ $<
	
obj/%.o: %.c | obj
	$(CC) $(CC_FLAGS) -c -o $@ $<
	
clean:
	$(RM) $(CC_OBJ) $(CC_EXE)
	
uninstall: clean
	$(RMDIR) bin obj
	
.PRECIOUS: obj/%.o
.PHONY: all debug release build clean uninstall
