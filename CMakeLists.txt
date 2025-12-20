cmake_minimum_required(VERSION 3.14)
project(cache-lib VERSION 1.0.0 LANGUAGES CXX)

# ==================== Настройки компиляции ====================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Предупреждения компилятора
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ==================== Библиотека кэширования ====================
# Header-only библиотека
add_library(cache INTERFACE)
target_include_directories(cache INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# ==================== Demo приложение ====================
add_executable(cache_demo
    demo/main.cpp
)
target_link_libraries(cache_demo PRIVATE cache)

# ==================== Бенчмарки ====================

# Базовый бенчмарк (LRU)
add_executable(cache_benchmark
    benchmarks/CacheBenchmark.cpp
)
target_link_libraries(cache_benchmark PRIVATE cache)

# Сравнительный бенчмарк LRU vs LFU
add_executable(cache_benchmark_lfu_vs_lru
    benchmarks/CacheBenchmarkLRUvsLFU.cpp
)
target_link_libraries(cache_benchmark_lfu_vs_lru PRIVATE cache)

# Бенчмарк конкурентного доступа
add_executable(concurrency_benchmark
    benchmarks/ConcurrencyBenchmark.cpp
)
target_link_libraries(concurrency_benchmark PRIVATE cache)

# ==================== Тестирование ====================
# Опция для включения/выключения тестов
option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
    # Подключаем GoogleTest
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    # Для Windows: предотвращаем переопределение опций родительского проекта
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    include(GoogleTest)

    # Все тесты в одном исполняемом файле
    add_executable(all_tests
        tests/CacheTest.cpp
        tests/LRUPolicyTest.cpp
        tests/LFUPolicyTest.cpp
        tests/CacheLFUTest.cpp
        tests/ListenersTest.cpp
        tests/GlobalTTLTest.cpp
        tests/NoExpirationTest.cpp
        tests/PerKeyTTLTest.cpp
        tests/ConcurrencyTest.cpp
        tests/ThreadSafeQueueTest.cpp
        tests/ThreadPerListenerCompositeTest.cpp
    )
    target_link_libraries(all_tests PRIVATE cache GTest::gtest_main)
    gtest_discover_tests(all_tests)
endif()

# ==================== Установка ====================
install(TARGETS cache
    EXPORT cacheTargets
    INCLUDES DESTINATION include
)

install(DIRECTORY include/
    DESTINATION include
)

install(EXPORT cacheTargets
    FILE cacheTargets.cmake
    NAMESPACE cache::
    DESTINATION lib/cmake/cache
)

# ==================== Информация о сборке ====================
message(STATUS "")
message(STATUS "=== Cache Library Configuration ===")
message(STATUS "Version:        ${PROJECT_VERSION}")
message(STATUS "C++ Standard:   ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Tests:    ${BUILD_TESTS}")
message(STATUS "Compiler:       ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "")