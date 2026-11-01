# Build rules for the parallel MCTS project.
# Recipes use relative paths only, so the build also works when the project's
# absolute path contains spaces (make does not quote such paths for you).
#
# Source layout (src/):
#   util/        small helpers: timing, random numbers, statistics, run info, CSV rows
#   parallel/    thread helpers: spin lock, thread-team launcher
#   games/       the game interface, Connect-4, and the match arena
#   search/      the Monte Carlo Tree Search implementations
#   demos/       small stand-alone demonstration programs
#   tests/       correctness tests (several also print speed tables)
#   benchmarks/  long-running strength measurements

CXX       := g++
CXXFLAGS  := -O2 -std=c++17 -fopenmp -pthread -Wall -Wextra
SRC_DIR   := src
BUILD_DIR := build

# Every header folder is on the include path, so a source file can write
# #include "rng.hpp" without naming the folder.
HEADER_DIRS := $(SRC_DIR)/util $(SRC_DIR)/parallel $(SRC_DIR)/games $(SRC_DIR)/search
INCLUDES    := $(addprefix -I,$(HEADER_DIRS))
HEADERS     := $(foreach d,$(HEADER_DIRS),$(wildcard $(d)/*.hpp))

# Programs live in these folders; vpath lets make find src/<folder>/<name>.cpp
# from the name alone.
vpath %.cpp $(SRC_DIR)/demos $(SRC_DIR)/tests $(SRC_DIR)/benchmarks

all: $(BUILD_DIR)/hello_threads $(BUILD_DIR)/test_connect4 $(BUILD_DIR)/test_uct $(BUILD_DIR)/test_parallel \
     $(BUILD_DIR)/race_demo $(BUILD_DIR)/test_locked $(BUILD_DIR)/strength_v3 \
     $(BUILD_DIR)/test_lockfree $(BUILD_DIR)/test_virtual_loss

# Pattern rule: build/<name> is compiled from <name>.cpp, and a change to any
# header rebuilds it.
# $< is the first prerequisite (the .cpp), $@ is the target (the binary).
# "| $(BUILD_DIR)" is an order-only prerequisite: the directory must exist,
# but its timestamp does not trigger a rebuild.
$(BUILD_DIR)/%: %.cpp $(HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run-hello: $(BUILD_DIR)/hello_threads
	./$(BUILD_DIR)/hello_threads

test-connect4: $(BUILD_DIR)/test_connect4
	./$(BUILD_DIR)/test_connect4

test-uct: $(BUILD_DIR)/test_uct
	./$(BUILD_DIR)/test_uct

test-parallel: $(BUILD_DIR)/test_parallel
	./$(BUILD_DIR)/test_parallel

race-demo: $(BUILD_DIR)/race_demo
	./$(BUILD_DIR)/race_demo

test-locked: $(BUILD_DIR)/test_locked
	./$(BUILD_DIR)/test_locked

# Usage: make strength-v3 ARGS="20 2 4"   (games per match, then thread counts)
strength-v3: $(BUILD_DIR)/strength_v3
	./$(BUILD_DIR)/strength_v3 $(ARGS)

test-lockfree: $(BUILD_DIR)/test_lockfree
	./$(BUILD_DIR)/test_lockfree

test-virtual-loss: $(BUILD_DIR)/test_virtual_loss
	./$(BUILD_DIR)/test_virtual_loss

# ThreadSanitizer build: std::thread instead of OpenMP, no optimisation tricks.
$(BUILD_DIR)/tsan_check: tsan_check.cpp $(HEADERS) | $(BUILD_DIR)
	$(CXX) -O1 -g -std=c++17 -pthread -fsanitize=thread -DMCTS_STD_THREADS -Wall -Wextra $(INCLUDES) $< -o $@

# setarch -R switches address-space randomisation off for this one process.
# Without it, ThreadSanitizer can stop with "unexpected memory mapping" on
# kernels that randomise memory addresses with 32 bits of entropy (as WSL2 does).
tsan: $(BUILD_DIR)/tsan_check
	setarch $$(uname -m) -R ./$(BUILD_DIR)/tsan_check

tsan-control: $(BUILD_DIR)/tsan_check
	setarch $$(uname -m) -R ./$(BUILD_DIR)/tsan_check control

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run-hello test-connect4 test-uct test-parallel race-demo test-locked strength-v3 test-lockfree test-virtual-loss tsan tsan-control clean
