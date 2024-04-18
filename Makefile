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

# -*- DEPENDENCY GENERATION ----------------------------------------------- {{{1
#
# -MT<arg>
#	Set the main target name to <arg>, unquoted.
#
# -MMD
#	Write a depfile containing user headers.
#
# -MM
#	Like -MMD, but also implies -E and writes to stdout by default.
#
# -MF <file>
#	Write depfile output from -MMD, -MD, -MM or -M to <file>
#
# -MP
#	Create phony targets for each dependency excluding the main target.
#
# See: https://make.mad-scientist.net/papers/advanced-auto-dependency-generation/
#
# ------------------------------------------------------------------------------

DEPDIR 	:= obj/.deps

# Very important to not expand this immediately using `:=` syntax!
DEPFLAGS = -MT '$@' -MMD -MP -MF $(DEPDIR)/$*.d

# 1}}} -------------------------------------------------------------------------

# -*- PREAMBLE -----------------------------------------------------------*- {{{

.PHONY: all
all: debug

.PHONY: debug
debug: CC_FLAGS += $(CC_DBGFLAGS)
debug: build

.PHONY: release
release: CC_FLAGS += -Os
release: LD_FLAGS += -s
release: build

# }}} --------------------------------------------------------------------------

# -*- TARGETS ------------------------------------------------------------*- {{{

.PHONY: build
build: bin/$(EXE)

src bin obj $(DEPDIR):
	$(MKDIR) $@

bin/$(EXE): $(CC_OBJ) | bin
	$(CC) $(CC_FLAGS) -o $@ $^ $(LD_FLAGS)

.PRECIOUS: obj/%.o
obj/%.o: src/%.c $(DEPDIR)/%.d | obj $(DEPDIR)
	$(CC) $(DEPFLAGS) $(CC_FLAGS) -c -o $@ $<

DEPFILES := $(CC_SRC:src/%.c=$(DEPDIR)/%.d)
$(DEPFILES):

include $(wildcard $(DEPFILES))

.PHONY: clean
clean:
	$(RM) $(CC_OBJ)

.PHONY: uninstall
uninstall: clean
	$(RM) bin/$(EXE) $(DEPFILES)
	$(RMDIR) $(DEPDIR) bin obj

# }}} --------------------------------------------------------------------------
