JOBS := $(shell nproc)
BUILD_DIR := build
PG_CONN := host=localhost port=5433 dbname=jobqueue user=jobqueue password=jobqueue

.PHONY: all build run start test tsan helgrind memcheck configure clean distclean migrate crash bench

all: build

## Configure the CMake build tree (idempotent, safe to re-run)
configure:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug

## Build the server and the test binary
build: configure
	cmake --build $(BUILD_DIR) -j$(JOBS)

## Build, then start the server on http://localhost:8080
run: build
	JOBQUEUE_PG_CONN="$(PG_CONN)" ./$(BUILD_DIR)/jobqueue

## Start the existing binary without rebuilding
start:
	@test -x $(BUILD_DIR)/jobqueue || { echo "No binary at $(BUILD_DIR)/jobqueue - run 'make build' first"; exit 1; }
	JOBQUEUE_PG_CONN="$(PG_CONN)" ./$(BUILD_DIR)/jobqueue

## k6 load test against a running server (start it with JOBQUEUE_RATE_LIMIT_RPS=0). See benchmarks/README.md.
bench:
	docker run --rm -i --network host -v "$(CURDIR)/benchmarks:/bench:ro" grafana/k6:1.1.0 run $(K6_ARGS) /bench/post_jobs.js

## Create the test DB if missing and apply migrations/*.sql to jobqueue and test.
## Works on existing volumes (initdb.d only runs on a fresh one). Migrations must be idempotent.
migrate:
	docker compose exec -T db psql -U jobqueue -d postgres -tAc \
		"SELECT 1 FROM pg_database WHERE datname = 'test'" | grep -q 1 || \
		docker compose exec -T db psql -U jobqueue -d postgres -c "CREATE DATABASE test"
	for db in jobqueue test; do \
		for f in migrations/*.sql; do \
			echo "$$db <- $$f"; \
			docker compose exec -T db psql -U jobqueue -d $$db -v ON_ERROR_STOP=1 -q < $$f || exit 1; \
		done; \
	done

## End-to-end crash checks against the test DB (restarts the Postgres container; see the script header)
crash: build
	scripts/crash_checks.py

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
