JOBS := $(shell nproc)
BUILD_DIR := build

.PHONY: all build run start test tsan helgrind memcheck configure clean distclean

all: build

## Configure the CMake build tree (idempotent, safe to re-run)
configure:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug

## Build the server and the test binary
build: configure
	cmake --build $(BUILD_DIR) -j$(JOBS)

## Build, then start the server on http://localhost:8080
run: build
	./$(BUILD_DIR)/jobqueue

## Start the existing binary without rebuilding
start:
	@test -x $(BUILD_DIR)/jobqueue || { echo "No binary at $(BUILD_DIR)/jobqueue - run 'make build' first"; exit 1; }
	./$(BUILD_DIR)/jobqueue

## Unit + integration tests
test: build
	./$(BUILD_DIR)/tests

## ThreadSanitizer build (excluded from the default build)
tsan: configure
	cmake --build $(BUILD_DIR) --target tests_tsan -j$(JOBS)
	./$(BUILD_DIR)/tests_tsan

## Helgrind: thread errors
helgrind: configure
	cmake --build $(BUILD_DIR) --target tests_helgrind -j$(JOBS)
	valgrind --tool=helgrind ./$(BUILD_DIR)/tests_helgrind

## Valgrind memcheck: memory leaks
memcheck: configure
	cmake --build $(BUILD_DIR) --target tests_helgrind -j$(JOBS)
	valgrind --leak-check=full --error-exitcode=1 ./$(BUILD_DIR)/tests_helgrind

## Remove build artifacts, keep the CMake cache
clean:
	cmake --build $(BUILD_DIR) --target clean

## Remove the whole build tree, including fetched dependencies
distclean:
	rm -rf $(BUILD_DIR)
