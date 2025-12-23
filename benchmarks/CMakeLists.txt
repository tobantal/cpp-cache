cmake_minimum_required(VERSION 3.16)
project(CacheLibraryComparison)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wall -Wextra")

# ========== DEPENDENCIES ==========

include(FetchContent)

# LRUCache11 (header-only) - базовая LRU реализация
FetchContent_Declare(lrucache11
    GIT_REPOSITORY https://github.com/mohaps/lrucache11.git
    GIT_TAG master
    DOWNLOAD_NO_PROGRESS ON
)
FetchContent_MakeAvailable(lrucache11)

# cpp-lru-cache (header-only) - простая LRU реализация
FetchContent_Declare(cpp_lru_cache
    GIT_REPOSITORY https://github.com/lamerman/cpp-lru-cache.git
    GIT_TAG master
    DOWNLOAD_NO_PROGRESS ON
)
FetchContent_Populate(cpp_lru_cache)

# ========== BENCHMARK EXECUTABLE ==========

add_executable(library_comparison
    comparison/LibraryComparison.cpp
)

target_include_directories(library_comparison PRIVATE
    ${CMAKE_SOURCE_DIR}/include          # наша библиотека
    ${lrucache11_SOURCE_DIR}             # lrucache11.hpp
    ${cpp_lru_cache_SOURCE_DIR}/include
)

target_link_libraries(library_comparison PRIVATE
    pthread
)

# ========== COMPILER FLAGS ==========

if(MSVC)
    target_compile_options(library_comparison PRIVATE /W4)
else()
    target_compile_options(library_comparison PRIVATE -Wall -Wextra -Wpedantic)
endif()

message(STATUS "Cache Library Comparison Benchmark configured successfully")
message(STATUS "Build with: cmake --build . --config Release")
message(STATUS "Run with: ./library_comparison")
