CXX := clang++
CXX_FLAGS := -std=c++17 -O0 -g -Wall -Wextra -Wconversion

SOURCES := $(wildcard src/*.cpp)
OBJECTS := $(patsubst src/%.cpp,obj/%.o,$(SOURCES))

bin/lulu: $(OBJECTS) | bin
	$(CXX) $(CXX_FLAGS) -o $@ $^

bin obj:
	mkdir -p $@

obj/%.o: src/%.cpp src/%.hpp | obj
	$(CXX) $(CXX_FLAGS) -c -o $@ $<

.PHONY: list
list:
	@echo SOURCES: $(SOURCES)
	@echo OBJECTS: $(OBJECTS)
