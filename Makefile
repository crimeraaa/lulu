EXE 		:= lulu
CC 			:= clang
CC_FLAGS	:= -std=c11 -Wall -Wextra -Werror -pedantic \
			-fdiagnostics-color=always -Wno-error=unused-variable \
			-Wno-error=unused-function -Wno-error=unused-parameter \
			-Wno-error=unused-but-set-variable
LD_FLAGS	:= -lm
CC_DBGFLAGS	:= -g -DDEBUG_PRINT_CODE -DDEBUG_TRACE_EXECUTION -DDEBUG_USE_ASSERT

CC_SRC 		:= $(wildcard src/*.c)
CC_OBJ 		:= $(patsubst src/%.c,obj/%.o,$(CC_SRC))
CC_INCLUDE 	:= $(wildcard src/*.h)

# -*- PREAMBLE -----------------------------------------------------------*- {{{

all: debug

debug: CC_FLAGS += $(CC_DBGFLAGS)
debug: build

release: CC_FLAGS += -Os
release: LD_FLAGS += -s
release: build

# }}} --------------------------------------------------------------------------

# -*- TARGETS ------------------------------------------------------------*- {{{

build: bin/$(EXE)

src bin obj:
	$(MKDIR) $@

bin/$(EXE): $(CC_OBJ) | bin
	$(CC) $(CC_FLAGS) -o $@ $^ $(LD_FLAGS)

obj/%.o: src/%.c src/%.h | obj
	$(CC) $(CC_FLAGS) -c -o $@ $<

clean:
	$(RM) $(CC_OBJ)

uninstall: clean
	$(RM) bin/$(EXE)
	$(RMDIR) bin obj

# }}} --------------------------------------------------------------------------

.PHONY: all build debug release clean uninstall
.PRECIOUS: obj/%.o
