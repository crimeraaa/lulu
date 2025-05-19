CC := clang
CC_FLAGS := -std=gnu89 -Wall -Wextra -g

FILE := lreadline
SRC  := $(FILE).c
INC  := $(FILE).h
OBJ  := $(FILE).o
LIB  := readline.so
SYMLINK := ../$(LIB)

.PHONY: all
all: $(LIB) $(SYMLINK)

# Convenience symlink: ln <original> <to-link>
$(SYMLINK): | $(LIB)
	ln --symbolic $(LIB) $@

$(LIB): $(OBJ)
	$(CC) $(CC_FLAGS) -shared -o $@ $< -Lreadline

# Use $< and not $^ because we only want to pass in the `.c` file
$(OBJ): $(SRC) $(INC)
	$(CC) $(CC_FLAGS) -c -o $@ $<

.PHONY: clean
clean:
	$(RM) $(OBJ) $(LIB)
