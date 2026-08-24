.PHONY: all toolchain dos host-test test perf-test run dist clean

HOST_CXX ?= g++
HOST_CXXFLAGS ?= -std=c++98 -Wall -Wextra -Werror -pedantic -O2

all: dos

toolchain:
	bash tools/bootstrap-watcom.sh

dos: toolchain
	bash tools/build.sh

build/test_game: tests/test_game.cpp src/game.cpp src/game.h
	mkdir -p build
	$(HOST_CXX) $(HOST_CXXFLAGS) -Isrc tests/test_game.cpp src/game.cpp -o $@

host-test: build/test_game
	./build/test_game

perf-test: dos
	bash tools/dosbox-perf-test.sh

test: dos host-test
	bash tools/dosbox-test.sh
	$(MAKE) perf-test

run: dos
	bash tools/run.sh

dist: test
	mkdir -p dist
	cp build/CUBOLOR.EXE README.TXT LICENSE dist/

clean:
	rm -rf build dist
