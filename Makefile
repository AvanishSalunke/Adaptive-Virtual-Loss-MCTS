# Build rules for the parallel MCTS project.
# Recipes use relative paths only, so the build also works when the project's
# absolute path contains spaces (make does not quote such paths for you).

CXX      := g++
CXXFLAGS := -O2 -std=c++17 -fopenmp -pthread -Wall -Wextra
SRC_DIR  := src
BUILD_DIR := build

all: $(BUILD_DIR)/hello_threads

# Demo program: starts OpenMP threads and times a fixed workload.
# $< is the first prerequisite (the .cpp), $@ is the target (the binary).
# "| $(BUILD_DIR)" is an order-only prerequisite: the directory must exist,
# but its timestamp does not trigger a rebuild.
$(BUILD_DIR)/hello_threads: $(SRC_DIR)/hello_threads.cpp $(SRC_DIR)/timing.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run-hello: $(BUILD_DIR)/hello_threads
	./$(BUILD_DIR)/hello_threads

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run-hello clean
