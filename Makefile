# Build rules for the parallel MCTS project.
# Recipes use relative paths only, so the build also works when the project's
# absolute path contains spaces (make does not quote such paths for you).

CXX      := g++
CXXFLAGS := -O2 -std=c++17 -fopenmp -pthread -Wall -Wextra
SRC_DIR  := src
BUILD_DIR := build
HEADERS  := $(wildcard $(SRC_DIR)/*.hpp)

all: $(BUILD_DIR)/hello_threads $(BUILD_DIR)/test_connect4 $(BUILD_DIR)/test_uct $(BUILD_DIR)/test_parallel \
     $(BUILD_DIR)/race_demo $(BUILD_DIR)/test_locked $(BUILD_DIR)/strength_v3

# Pattern rule: build/<name> is compiled from src/<name>.cpp, and a change to
# any header rebuilds it.
# $< is the first prerequisite (the .cpp), $@ is the target (the binary).
# "| $(BUILD_DIR)" is an order-only prerequisite: the directory must exist,
# but its timestamp does not trigger a rebuild.
$(BUILD_DIR)/%: $(SRC_DIR)/%.cpp $(HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@

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

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run-hello test-connect4 test-uct test-parallel race-demo test-locked strength-v3 clean
