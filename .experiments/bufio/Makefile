DIR_SRC := .
DIR_OBJ := ./obj
DIR_BIN := ./bin

CXX 	:= clang++
CXX_FLAGS := $(CXXFLAGS) -Wno-error=unused-function -Wno-error=unused-parameter
CXX_SRC := $(wildcard $(DIR_SRC)/*.cpp)
CXX_OBJ := $(patsubst $(DIR_SRC)/%.cpp,$(DIR_OBJ)/%.o,$(CXX_SRC))
CXX_INC := $(wildcard *.hpp)
CXX_EXE := $(DIR_BIN)/main

.PHONY: all
all: build
	
.PHONY: build
build: $(CXX_EXE)
	
$(DIR_OBJ) $(DIR_BIN):
	$(MKDIR) $@
	
$(CXX_EXE): $(CXX_OBJ) | $(DIR_BIN)
	$(CXX) $(CXX_FLAGS) -o $@ $^

.PRECIOUS: $(DIR_OBJ)/%.o
$(DIR_OBJ)/%.o: $(DIR_SRC)/%.cpp $(DIR_SRC)/%.hpp | $(DIR_OBJ)
	$(CXX) $(CXX_FLAGS) -c -o $@ $<
	
# Stupid but better than nothing
conf.hpp:
global.hpp: mem.hpp
mem.hpp: conf.hpp
io.hpp: conf.hpp mem.hpp
main.hpp: conf.hpp io.hpp mem.hpp lexer.hpp global.hpp
lexer.hpp: io.hpp global.hpp

.PHONY: clean
clean:
	$(RM) $(CXX_EXE) $(CXX_OBJ)
	
.PHONY: clean
uninstall: clean
	$(RMDIR) $(DIR_BIN) $(DIR_OBJ)

