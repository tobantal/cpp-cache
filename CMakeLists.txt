cmake_minimum_required(VERSION 3.16)
project(cache-lib VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Wpedantic)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)
enable_testing()
include(GoogleTest)

# Tests
file(GLOB TEST_SOURCES tests/*.cpp)
add_executable(cache-tests ${TEST_SOURCES})
target_link_libraries(cache-tests GTest::gtest_main)
gtest_discover_tests(cache-tests)

# Demo
add_executable(cache-demo demo/main.cpp)
target_include_directories(cache-demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/demo)

# Benchmarks
file(GLOB BENCH_SOURCES benchmarks/*.cpp)
add_executable(cache-benchmark ${BENCH_SOURCES})