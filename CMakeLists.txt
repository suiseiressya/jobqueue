cmake_minimum_required(VERSION 3.20)
project(jobqueue LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# cpp-httplib (header-only HTTP server)
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.3
)
FetchContent_MakeAvailable(httplib)

# nlohmann/json (header-only JSON library)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# libuuid for UUID generation
find_package(PkgConfig REQUIRED)
pkg_check_modules(UUID REQUIRED IMPORTED_TARGET uuid)

add_executable(jobqueue
    src/main.cc
    src/job_queue.cc
    src/job_service.cc
    src/http_server.cc
    src/job_id.cc
    src/thread_pool.cc
)

target_include_directories(jobqueue PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(jobqueue
    PRIVATE
        httplib::httplib
        nlohmann_json::nlohmann_json
        PkgConfig::UUID
        pthread
)

# Testing
enable_testing()

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
)
FetchContent_MakeAvailable(Catch2)

add_executable(tests
    tests/job_queue_test.cc
    tests/job_service_test.cc
    tests/http_server_test.cc
    tests/thread_pool_test.cc
    src/http_server.cc
    src/job_service.cc
    src/job_queue.cc
    src/job_id.cc
    src/thread_pool.cc
)

target_include_directories(tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tests
    PRIVATE
        Catch2::Catch2WithMain
        httplib::httplib
        nlohmann_json::nlohmann_json
        PkgConfig::UUID
        pthread
)

include(CTest)
include(Catch)
catch_discover_tests(tests)

# TSan build
add_executable(tests_tsan EXCLUDE_FROM_ALL
    tests/job_service_test.cc
    tests/thread_pool_test.cc
    src/job_service.cc
    src/job_queue.cc
    src/job_id.cc
    src/thread_pool.cc
    src/http_server.cc
)
target_include_directories(tests_tsan PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(tests_tsan PRIVATE -fsanitize=thread -g)
target_link_options(tests_tsan PRIVATE -fsanitize=thread)
target_link_libraries(tests_tsan
    PRIVATE
        Catch2::Catch2WithMain
        httplib::httplib
        nlohmann_json::nlohmann_json
        PkgConfig::UUID
        pthread
)

# Helgrind-compatible build (debug symbols, no TSan)
add_executable(tests_helgrind EXCLUDE_FROM_ALL
    tests/job_service_test.cc
    tests/thread_pool_test.cc
    src/job_service.cc
    src/job_queue.cc
    src/job_id.cc
    src/thread_pool.cc
    src/http_server.cc
)
target_include_directories(tests_helgrind PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(tests_helgrind PRIVATE -g -O0)
target_link_libraries(tests_helgrind
    PRIVATE
        Catch2::Catch2WithMain
        httplib::httplib
        nlohmann_json::nlohmann_json
        PkgConfig::UUID
        pthread
)
