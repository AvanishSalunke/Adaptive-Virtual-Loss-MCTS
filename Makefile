# Build rules for the parallel MCTS project.
# Recipes use relative paths only, so the build also works when the project's
# absolute path contains spaces (make does not quote such paths for you).
#
# Source layout (src/):
#   util/         small helpers: timing, random numbers, statistics, run info, CSV rows
#   parallel/     the thread-team launcher
#   games/        the game interface, Connect-4, and the match arena
#   search/       what the final comparison needs: sequential UCT (V0), the lock-free
#                 tree (V4) with fixed or adaptive virtual loss (V5), the penalty rule
#   tests/        correctness tests
#   benchmarks/   the final strength comparison
#   experiments/  everything else: the earlier parallel versions (leaf, root, locks),
#                 spin lock, and finished measurements kept as evidence for the paper
#                 (build them with `make experiments`)

CXX       := g++
CXXFLAGS  := -O2 -std=c++17 -fopenmp -pthread -Wall -Wextra
SRC_DIR   := src
BUILD_DIR := build

# Every header folder is on the include path, so a source file can write
# #include "rng.hpp" without naming the folder.
HEADER_DIRS := $(SRC_DIR)/util $(SRC_DIR)/parallel $(SRC_DIR)/games $(SRC_DIR)/search $(SRC_DIR)/experiments
INCLUDES    := $(addprefix -I,$(HEADER_DIRS))
HEADERS     := $(foreach d,$(HEADER_DIRS),$(wildcard $(d)/*.hpp))

# Programs live in these folders; vpath lets make find src/<folder>/<name>.cpp
# from the name alone.
vpath %.cpp $(SRC_DIR)/tests $(SRC_DIR)/benchmarks $(SRC_DIR)/experiments

all: $(BUILD_DIR)/test_connect4 $(BUILD_DIR)/test_uct $(BUILD_DIR)/compare

experiments: $(BUILD_DIR)/hello_threads $(BUILD_DIR)/race_demo $(BUILD_DIR)/test_parallel \
             $(BUILD_DIR)/test_locked $(BUILD_DIR)/test_lockfree $(BUILD_DIR)/test_virtual_loss \
             $(BUILD_DIR)/strength_v3

# Pattern rule: build/<name> is compiled from <name>.cpp, and a change to any
# header rebuilds it.
# $< is the first prerequisite (the .cpp), $@ is the target (the binary).
# "| $(BUILD_DIR)" is an order-only prerequisite: the directory must exist,
# but its timestamp does not trigger a rebuild.
$(BUILD_DIR)/%: %.cpp $(HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

test-connect4: $(BUILD_DIR)/test_connect4
	./$(BUILD_DIR)/test_connect4

test-uct: $(BUILD_DIR)/test_uct
	./$(BUILD_DIR)/test_uct

# The final comparison. Usage:
#   make compare-tune  ARGS="8 100"            (threads, games per candidate)
#   make compare-final ARGS="8 400 1.0 3"      (threads, games, lambda, fixed v)
compare-tune: $(BUILD_DIR)/compare
	./$(BUILD_DIR)/compare tune $(ARGS)

compare-final: $(BUILD_DIR)/compare
	./$(BUILD_DIR)/compare final $(ARGS)

# Finished measurements (see src/experiments/).
run-hello: $(BUILD_DIR)/hello_threads
	./$(BUILD_DIR)/hello_threads

test-parallel: $(BUILD_DIR)/test_parallel
	./$(BUILD_DIR)/test_parallel

race-demo: $(BUILD_DIR)/race_demo
	./$(BUILD_DIR)/race_demo

test-locked: $(BUILD_DIR)/test_locked
	./$(BUILD_DIR)/test_locked

test-lockfree: $(BUILD_DIR)/test_lockfree
	./$(BUILD_DIR)/test_lockfree

test-virtual-loss: $(BUILD_DIR)/test_virtual_loss
	./$(BUILD_DIR)/test_virtual_loss

# Usage: make strength-v3 ARGS="20 2 4"   (games per match, then thread counts)
strength-v3: $(BUILD_DIR)/strength_v3
	./$(BUILD_DIR)/strength_v3 $(ARGS)

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

.PHONY: all experiments test-connect4 test-uct compare-tune compare-final run-hello test-parallel race-demo test-locked test-lockfree test-virtual-loss strength-v3 tsan tsan-control clean
