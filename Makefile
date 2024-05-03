DIR_SRC		:= src
DIR_OBJ		:= obj
DIR_BIN		:= bin
DIR_DEP 	:= $(DIR_OBJ)/.deps

CC 			:= clang
CC_FLAGS	:= -std=c11 -Wall -Wextra -Werror -pedantic \
			-fdiagnostics-color=always -Wno-error=unused-variable \
			-Wno-error=unused-function -Wno-error=unused-parameter \
			-Wno-error=unused-but-set-variable
LD_FLAGS	:= -lm
CC_DBGFLAGS	:= -g -DDEBUG_PRINT_CODE -DDEBUG_TRACE_EXECUTION -DDEBUG_USE_ASSERT

CC_EXE		:= $(DIR_BIN)/lulu
CC_SRC 		:= $(wildcard $(DIR_SRC)/*.c)
CC_OBJ 		:= $(patsubst $(DIR_SRC)/%.c,$(DIR_OBJ)/%.o,$(CC_SRC))
CC_INCLUDE 	:= $(wildcard $(DIR_SRC)/*.h)

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

# Very important to not expand this immediately using `:=` syntax!
DEPFLAGS = -MT '$@' -MMD -MP -MF $(DIR_DEP)/$*.d

# 1}}} -------------------------------------------------------------------------

# -*- PREAMBLE -----------------------------------------------------------*- {{{

.PHONY: all
all: debug

.PHONY: debug
debug: CC_FLAGS += $(CC_DBGFLAGS)
debug: build

.PHONY: release
release: CC_FLAGS += -O2
release: LD_FLAGS += -s
release: build

.PHONY: run
run: build
	$(CC_EXE)

# }}} --------------------------------------------------------------------------

# -*- TARGETS ------------------------------------------------------------*- {{{

.PHONY: build
build: $(CC_EXE)

src bin obj $(DIR_DEP):
	$(MKDIR) $@

$(CC_EXE): $(CC_OBJ) | $(DIR_BIN)
	$(CC) $(CC_FLAGS) -o $@ $^ $(LD_FLAGS)

.PRECIOUS: $(DIR_OBJ)/%.o
$(DIR_OBJ)/%.o: $(DIR_SRC)/%.c $(DIR_DEP)/%.d | $(DIR_OBJ) $(DIR_DEP)
	$(CC) $(DEPFLAGS) $(CC_FLAGS) -c -o $@ $<

CC_DEP := $(CC_SRC:$(DIR_SRC)/%.c=$(DIR_DEP)/%.d)
$(CC_DEP):

include $(wildcard $(CC_DEP))

.PHONY: clean
clean:
	$(RM) $(CC_OBJ)

.PHONY: uninstall
uninstall: clean
	$(RM) $(CC_EXE) $(CC_DEP)
	$(RMDIR) $(DIR_DEP) bin obj

# }}} --------------------------------------------------------------------------
