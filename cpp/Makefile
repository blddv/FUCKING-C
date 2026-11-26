# SPDX-License-Identifier: WTFPL
CXX ?= c++
CPPFLAGS += -Iinclude
CXXFLAGS ?= -O2 -std=c++20 -Wall -Wextra -Wpedantic
LDFLAGS ?=

CORE = build/syntax.o build/runtime.o build/cli.o

.PHONY: all test clean sanitize
all: fc

fc: $(CORE) build/main.o
	$(CXX) $^ $(LDFLAGS) -o $@

fc_tests: $(CORE) build/test_language.o
	$(CXX) $^ $(LDFLAGS) -o $@

build/%.o: src/%.cpp src/internal.hpp include/fucking_c.hpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

build/test_language.o: tests/test_language.cpp include/fucking_c.hpp | build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

build:
	mkdir -p build

test: fc fc_tests
	./fc_tests examples

sanitize:
	cmake -S . -B build/sanitize -DCMAKE_BUILD_TYPE=Debug -DFC_ENABLE_SANITIZERS=ON
	cmake --build build/sanitize --parallel 2
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ctest --test-dir build/sanitize --output-on-failure

clean:
	rm -rf build fc fc_tests

-include build/*.d
