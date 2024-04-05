CXX			:= clang++
CXX_FLAGS 	:= -O0 -g -std=c++17 -Wall -Wextra -Werror -pedantic \
			-fdiagnostics-color=always -Wno-error=unused-variable \
			-Wno-error=unused-parameter -Wno-error=unused-function
CXX_SRC	 	:= $(wildcard *.cpp)
EXE_LIST	:= $(patsubst %.cpp,bin/%,$(CXX_SRC))

# -*- TARGETS ------------------------------------------------------------- {{{1

all: $(EXE_LIST)
	
bin:
	$(MKDIR) $@
	
bin/%: %.cpp | bin
	$(CXX) $(CXX_FLAGS) -o $@ $<
	
clean:
	$(RM) $(EXE_LIST)
	
uninstall: clean
	$(RMDIR) bin obj

# 1}}} -------------------------------------------------------------------------

.PRECIOUS: obj/%.o
