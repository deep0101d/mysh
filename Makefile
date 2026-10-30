CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -g -Iinclude -MMD -MP
LDFLAGS  :=

# GNU readline gives arrow-key history, line editing and Tab completion.
# Auto-detected. Force with `make READLINE=1` / `make READLINE=0` (then `make clean`).
# Install on Debian/Ubuntu:  sudo apt install libreadline-dev
READLINE ?= $(shell printf '\#include <stdio.h>\n\#include <readline/readline.h>\n\#include <readline/history.h>\nint main(){return 0;}\n' | $(CXX) -x c++ - -lreadline -o /dev/null >/dev/null 2>&1 && echo 1 || echo 0)
ifeq ($(READLINE),1)
  CXXFLAGS += -DHAVE_READLINE
  LDFLAGS  += -lreadline
endif

SRC := $(wildcard src/*.cpp)
OBJ := $(SRC:src/%.cpp=build/%.o)

# Modules the unit tests link against (no shell state, no readline).
TEST_LIB := build/lexer.o build/parser.o build/utils.o
TEST_BIN := build/test_lexer build/test_parser

.PHONY: all test unit scripts pty clean run

all: mysh

mysh: $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS)

build/%.o: src/%.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/test_%: tests/test_%.cpp $(TEST_LIB)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $< $(TEST_LIB) -o $@

test: unit scripts pty

unit: $(TEST_BIN)
	@for t in $(TEST_BIN); do ./$$t || exit 1; done

scripts: mysh
	@./tests/run_scripts.sh ./mysh

pty: mysh
	@python3 tests/pty_jobs.py ./mysh

run: mysh
	./mysh

clean:
	rm -rf build mysh

-include $(OBJ:.o=.d)
