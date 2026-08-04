# Job Queue

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

## Run

```bash
./build/jobqueue
```

Server starts on `http://localhost:8080`.

## Tests

```bash
# Unit + integration tests
./build/tests

# Memcheck (memory leaks)
valgrind --leak-check=full --error-exitcode=1 ./build/tests_helgrind

# Helgrind (thread errors)
valgrind --tool=helgrind ./build/tests_helgrind

# ThreadSanitizer
cmake --build build --target tests_tsan
./build/tests_tsan
```