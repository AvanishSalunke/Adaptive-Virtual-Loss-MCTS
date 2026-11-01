# Builds and runs the final comparison: adaptive virtual loss (V5) against
# fixed virtual loss (V4, the non-adaptive baseline), on Connect-4.
# Everything needed is inside this folder. Run the commands from here.
#
#   make            build the program
#   make run        quick run (20 games, about a minute)
#   make run-full   the full run (400 games, about 15 minutes)
#   make clean      delete the built program

CXX      := g++
CXXFLAGS := -O2 -std=c++17 -fopenmp -pthread -Wall -Wextra
INCLUDES := -Igame -Isearch -Isupport
HEADERS  := $(wildcard game/*.hpp search/*.hpp support/*.hpp)

# Settings chosen by the tuning run: 8 threads each, adaptive lambda = 0.5, fixed v = 5.
THREADS := 8
LAMBDA  := 0.5
FIXED_V := 5

all: build/compare

build/compare: compare.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

build:
	mkdir -p build

run: build/compare
	./build/compare $(THREADS) 20 $(LAMBDA) $(FIXED_V)

run-full: build/compare
	./build/compare $(THREADS) 400 $(LAMBDA) $(FIXED_V)

clean:
	rm -rf build

.PHONY: all run run-full clean
