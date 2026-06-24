cmake_minimum_required(VERSION 3.20)
project(jobqueue LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
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
    src/main.cpp
    src/job_queue.cpp
    src/job_service.cpp
    src/http_server.cpp
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
    tests/test_job_queue.cpp
    tests/test_job_service.cpp
)

target_include_directories(tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tests
    PRIVATE
        Catch2::Catch2WithMain
        nlohmann_json::nlohmann_json
        PkgConfig::UUID
        pthread
)

include(CTest)
include(Catch)
catch_discover_tests(tests)
