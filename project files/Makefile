# Builds and runs the final comparison: adaptive virtual loss (V5) against
# fixed virtual loss (V4) and against the sequential base (V0), on Connect-4.
# Everything needed is inside this folder. Run the commands from here.
#
#   make            build the program
#   make run        quick demonstration (about 3 minutes, 20 games per match)
#   make run-full   the full comparison (400 games per match, about an hour)
#   make tune       the tuning run that chose the settings (about 30 minutes)
#   make clean      delete the built program

CXX      := g++
CXXFLAGS := -O2 -std=c++17 -fopenmp -pthread -Wall -Wextra
INCLUDES := -Igame -Isearch -Isupport
HEADERS  := $(wildcard game/*.hpp search/*.hpp support/*.hpp)

# Settings frozen by the tuning run: adaptive lambda = 0.5, fixed v = 5, 8 threads.
THREADS := 8
LAMBDA  := 0.5
FIXED_V := 5

all: build/compare

build/compare: compare.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

build:
	mkdir -p build

run: build/compare
	./build/compare final $(THREADS) 20 $(LAMBDA) $(FIXED_V)

run-full: build/compare
	./build/compare final $(THREADS) 400 $(LAMBDA) $(FIXED_V)

tune: build/compare
	./build/compare tune $(THREADS) 100

clean:
	rm -rf build

.PHONY: all run run-full tune clean
