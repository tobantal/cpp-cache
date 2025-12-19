#include <cache/Cache.hpp>
#include <cache/eviction/LRUPolicy.hpp>
#include <cache/concurrency/ThreadSafeCache.hpp>
#include <cache/concurrency/ShardedCache.hpp>

#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <random>
#include <functional>
#include <string>

/**
 * @brief Бенчмарк многопоточности кэша
 * 
 * Сравниваем:
 * 1. ThreadSafeCache — один mutex на весь кэш
 * 2. ShardedCache<4>  — 4 шарда
 * 3. ShardedCache<8>  — 8 шардов
 * 4. ShardedCache<16> — 16 шардов
 * 5. ShardedCache<32> — 32 шарда
 * 
 * Сценарии:
 * - Write-only (100% put)
 * - Read-only (100% get, предзаполненный кэш)
 * - Mixed (80% get, 20% put)
 * - Hot keys (много потоков на ограниченный набор ключей)
 */

// ==================== Утилиты ====================

using Clock = std::chrono::high_resolution_clock;
using Duration = std::chrono::duration<double, std::milli>;

struct BenchmarkResult {
    std::string name;
    int threads;
    double timeMs;
    double opsPerSec;
    size_t totalOps;
};

void printHeader() {
    std::cout << std::left 
              << std::setw(25) << "Cache Type"
              << std::setw(10) << "Threads"
              << std::setw(15) << "Time (ms)"
              << std::setw(18) << "Throughput"
              << std::setw(12) << "Speedup"
              << "\n";
    std::cout << std::string(80, '-') << "\n";
}

void printResult(const BenchmarkResult& result, double baselineOps = 0) {
    std::cout << std::left 
              << std::setw(25) << result.name
              << std::setw(10) << result.threads
              << std::fixed << std::setprecision(1)
              << std::setw(15) << result.timeMs
              << std::setw(18) << std::setprecision(0) << result.opsPerSec;
    
    if (baselineOps > 0) {
        std::cout << std::setprecision(2) << (result.opsPerSec / baselineOps) << "x";
    }
    std::cout << "\n";
}

template<typename K, typename V>
std::unique_ptr<Cache<K, V>> makeLRUCache(size_t capacity) {
    return std::make_unique<Cache<K, V>>(
        capacity, std::make_unique<LRUPolicy<K>>());
}

// ==================== Бенчмарки ====================

/**
 * @brief Бенчмарк записи (put)
 * Каждый поток пишет свой диапазон ключей
 */
template<typename CachePtr>
BenchmarkResult benchmarkWrite(CachePtr& cache, const std::string& name, 
                                int numThreads, int opsPerThread) {
    std::vector<std::thread> threads;
    
    auto start = Clock::now();
    
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&cache, t, opsPerThread]() {
            for (int i = 0; i < opsPerThread; ++i) {
                int key = t * opsPerThread + i;
                cache->put(key, key * 2);
            }
        });
    }
    
    for (auto& th : threads) {
        th.join();
    }
    
    auto end = Clock::now();
    Duration duration = end - start;
    
    size_t totalOps = static_cast<size_t>(numThreads) * opsPerThread;
    double opsPerSec = (totalOps / duration.count()) * 1000.0;
    
    return {name, numThreads, duration.count(), opsPerSec, totalOps};
}

/**
 * @brief Бенчмарк чтения (get)
 * Кэш предзаполнен, каждый поток читает случайные ключи
 */
template<typename CachePtr>
BenchmarkResult benchmarkRead(CachePtr& cache, const std::string& name,
                               int numThreads, int opsPerThread, int keyRange) {
    // Предзаполняем
    for (int i = 0; i < keyRange; ++i) {
        cache->put(i, i * 2);
    }
    
    std::vector<std::thread> threads;
    std::atomic<int> hits{0};
    
    auto start = Clock::now();
    
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&cache, &hits, t, opsPerThread, keyRange]() {
            std::mt19937 rng(42 + t);
            std::uniform_int_distribution<int> dist(0, keyRange - 1);
            
            for (int i = 0; i < opsPerThread; ++i) {
                int key = dist(rng);
                if (cache->get(key).has_value()) {
                    ++hits;
                }
            }
        });
    }
    
    for (auto& th : threads) {
        th.join();
    }
    
    auto end = Clock::now();
    Duration duration = end - start;
    
    size_t totalOps = static_cast<size_t>(numThreads) * opsPerThread;
    double opsPerSec = (totalOps / duration.count()) * 1000.0;
    
    return {name, numThreads, duration.count(), opsPerSec, totalOps};
}

/**
 * @brief Смешанная нагрузка (80% read, 20% write)
 */
template<typename CachePtr>
BenchmarkResult benchmarkMixed(CachePtr& cache, const std::string& name,
                                int numThreads, int opsPerThread, int keyRange) {
    // Предзаполняем частично
    for (int i = 0; i < keyRange / 2; ++i) {
        cache->put(i, i);
    }
    
    std::vector<std::thread> threads;
    
    auto start = Clock::now();
    
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&cache, t, opsPerThread, keyRange]() {
            std::mt19937 rng(42 + t);
            std::uniform_int_distribution<int> keyDist(0, keyRange - 1);
            std::uniform_int_distribution<int> opDist(0, 99);
            
            for (int i = 0; i < opsPerThread; ++i) {
                int key = keyDist(rng);
                if (opDist(rng) < 80) {
                    cache->get(key);
                } else {
                    cache->put(key, i);
                }
            }
        });
    }
    
    for (auto& th : threads) {
        th.join();
    }
    
    auto end = Clock::now();
    Duration duration = end - start;
    
    size_t totalOps = static_cast<size_t>(numThreads) * opsPerThread;
    double opsPerSec = (totalOps / duration.count()) * 1000.0;
    
    return {name, numThreads, duration.count(), opsPerSec, totalOps};
}

/**
 * @brief Высокая конкуренция — много потоков, мало ключей
 */
template<typename CachePtr>
BenchmarkResult benchmarkHotKeys(CachePtr& cache, const std::string& name,
                                  int numThreads, int opsPerThread, int hotKeyCount) {
    std::vector<std::thread> threads;
    
    auto start = Clock::now();
    
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&cache, t, opsPerThread, hotKeyCount]() {
            std::mt19937 rng(42 + t);
            std::uniform_int_distribution<int> keyDist(0, hotKeyCount - 1);
            std::uniform_int_distribution<int> opDist(0, 1);
            
            for (int i = 0; i < opsPerThread; ++i) {
                int key = keyDist(rng);
                if (opDist(rng) == 0) {
                    cache->get(key);
                } else {
                    cache->put(key, i);
                }
            }
        });
    }
    
    for (auto& th : threads) {
        th.join();
    }
    
    auto end = Clock::now();
    Duration duration = end - start;
    
    size_t totalOps = static_cast<size_t>(numThreads) * opsPerThread;
    double opsPerSec = (totalOps / duration.count()) * 1000.0;
    
    return {name, numThreads, duration.count(), opsPerSec, totalOps};
}

// ==================== Фабрики кэшей ====================

auto makeThreadSafe(size_t capacity) {
    return std::make_unique<ThreadSafeCache<int, int>>(
        makeLRUCache<int, int>(capacity));
}

template<size_t Shards>
auto makeSharded(size_t capacity) {
    auto factory = [](size_t cap) {
        return makeLRUCache<int, int>(cap);
    };
    return std::make_unique<ShardedCache<int, int, Shards>>(capacity, factory);
}

// ==================== Запуск бенчмарков ====================

void runWriteBenchmark() {
    std::cout << "\n=== WRITE BENCHMARK (100% put) ===\n\n";
    
    const int OPS_PER_THREAD = 50000;
    const size_t CACHE_SIZE = 100000;
    std::vector<int> threadCounts = {1, 2, 4, 8, 16};
    
    for (int numThreads : threadCounts) {
        std::cout << "Threads: " << numThreads << "\n";
        printHeader();
        
        double baseline = 0;
        
        // ThreadSafeCache
        {
            auto cache = makeThreadSafe(CACHE_SIZE);
            auto result = benchmarkWrite(cache, "ThreadSafeCache", numThreads, OPS_PER_THREAD);
            if (numThreads == 1) baseline = result.opsPerSec;
            printResult(result, baseline);
        }
        
        // ShardedCache с разным количеством шардов
        {
            auto cache = makeSharded<4>(CACHE_SIZE);
            auto result = benchmarkWrite(cache, "ShardedCache<4>", numThreads, OPS_PER_THREAD);
            printResult(result, baseline);
        }
        {
            auto cache = makeSharded<8>(CACHE_SIZE);
            auto result = benchmarkWrite(cache, "ShardedCache<8>", numThreads, OPS_PER_THREAD);
            printResult(result, baseline);
        }
        {
            auto cache = makeSharded<16>(CACHE_SIZE);
            auto result = benchmarkWrite(cache, "ShardedCache<16>", numThreads, OPS_PER_THREAD);
            printResult(result, baseline);
        }
        {
            auto cache = makeSharded<32>(CACHE_SIZE);
            auto result = benchmarkWrite(cache, "ShardedCache<32>", numThreads, OPS_PER_THREAD);
            printResult(result, baseline);
        }
        
        std::cout << "\n";
    }
}

void runReadBenchmark() {
    std::cout << "\n=== READ BENCHMARK (100% get, pre-filled) ===\n\n";
    
    const int OPS_PER_THREAD = 50000;
    const size_t CACHE_SIZE = 50000;
    const int KEY_RANGE = 50000;
    std::vector<int> threadCounts = {1, 2, 4, 8, 16};
    
    for (int numThreads : threadCounts) {
        std::cout << "Threads: " << numThreads << "\n";
        printHeader();
        
        double baseline = 0;
        
        {
            auto cache = makeThreadSafe(CACHE_SIZE);
            auto result = benchmarkRead(cache, "ThreadSafeCache", numThreads, OPS_PER_THREAD, KEY_RANGE);
            if (numThreads == 1) baseline = result.opsPerSec;
            printResult(result, baseline);
        }
        {
            auto cache = makeSharded<4>(CACHE_SIZE);
            auto result = benchmarkRead(cache, "ShardedCache<4>", numThreads, OPS_PER_THREAD, KEY_RANGE);
            printResult(result, baseline);
        }
        {
            auto cache = makeSharded<8>(CACHE_SIZE);
            auto result = benchmarkRead(cache, "ShardedCache<8>", numThreads, OPS_PER_THREAD, KEY_RANGE);
            printResult(result, baseline);
        }
        {
            auto cache = makeSharded<16>(CACHE_SIZE);
            auto result = benchmarkRead(cache, "ShardedCache<16>", numThreads, OPS_PER_THREAD, KEY_RANGE);
            printResult(result, baseline);
        }
        {
            auto cache = makeSharded<32>(CACHE_SIZE);
            auto result = benchmarkRead(cache, "ShardedCache<32>", numThreads, OPS_PER_THREAD, KEY_RANGE);
            printResult(result, baseline);
        }
        
        std::cout << "\n";
    }
}

void runMixedBenchmark() {
    std::cout << "\n=== MIXED BENCHMARK (80% read, 20% write) ===\n\n";
    
    const int OPS_PER_THREAD = 50000;
    const size_t CACHE_SIZE = 50000;
    const int KEY_RANGE = 50000;
    std::vector<int> threadCounts = {1, 2, 4, 8, 16};
    
    for (int numThreads : threadCounts) {
        std::cout << "Threads: " << numThreads << "\n";
        printHeader();
        
        double baseline = 0;
        
        {
            auto cache = makeThreadSafe(CACHE_SIZE);
            auto result = benchmarkMixed(cache, "ThreadSafeCache", numThreads, OPS_PER_THREAD, KEY_RANGE);
            if (numThreads == 1) baseline = result.opsPerSec;
            printResult(result, baseline);
        }
        {
            auto cache = makeSharded<4>(CACHE_SIZE);
            auto result = benchmarkMixed(cache, "ShardedCache<4>", numThreads, OPS_PER_THREAD, KEY_RANGE);
            printResult(result, baseline);
        }
        {
            auto cache = makeSharded<8>(CACHE_SIZE);
            auto result = benchmarkMixed(cache, "ShardedCache<8>", numThreads, OPS_PER_THREAD, KEY_RANGE);
            printResult(result, baseline);
        }
        {
            auto cache = makeSharded<16>(CACHE_SIZE);
            auto result = benchmarkMixed(cache, "ShardedCache<16>", numThreads, OPS_PER_THREAD, KEY_RANGE);
            printResult(result, baseline);
        }
        {
            auto cache = makeSharded<32>(CACHE_SIZE);
            auto result = benchmarkMixed(cache, "ShardedCache<32>", numThreads, OPS_PER_THREAD, KEY_RANGE);
            printResult(result, baseline);
        }
        
        std::cout << "\n";
    }
}

void runHotKeysBenchmark() {
    std::cout << "\n=== HOT KEYS BENCHMARK (high contention) ===\n\n";
    
    const int OPS_PER_THREAD = 50000;
    const size_t CACHE_SIZE = 1000;
    const int HOT_KEYS = 10;  // Очень мало ключей — высокая конкуренция
    std::vector<int> threadCounts = {1, 2, 4, 8, 16};
    
    for (int numThreads : threadCounts) {
        std::cout << "Threads: " << numThreads << ", Hot keys: " << HOT_KEYS << "\n";
        printHeader();
        
        double baseline = 0;
        
        {
            auto cache = makeThreadSafe(CACHE_SIZE);
            auto result = benchmarkHotKeys(cache, "ThreadSafeCache", numThreads, OPS_PER_THREAD, HOT_KEYS);
            if (numThreads == 1) baseline = result.opsPerSec;
            printResult(result, baseline);
        }
        {
            auto cache = makeSharded<4>(CACHE_SIZE);
            auto result = benchmarkHotKeys(cache, "ShardedCache<4>", numThreads, OPS_PER_THREAD, HOT_KEYS);
            printResult(result, baseline);
        }
        {
            auto cache = makeSharded<8>(CACHE_SIZE);
            auto result = benchmarkHotKeys(cache, "ShardedCache<8>", numThreads, OPS_PER_THREAD, HOT_KEYS);
            printResult(result, baseline);
        }
        {
            auto cache = makeSharded<16>(CACHE_SIZE);
            auto result = benchmarkHotKeys(cache, "ShardedCache<16>", numThreads, OPS_PER_THREAD, HOT_KEYS);
            printResult(result, baseline);
        }
        {
            auto cache = makeSharded<32>(CACHE_SIZE);
            auto result = benchmarkHotKeys(cache, "ShardedCache<32>", numThreads, OPS_PER_THREAD, HOT_KEYS);
            printResult(result, baseline);
        }
        
        std::cout << "\n";
    }
}

void runScalabilityTest() {
    std::cout << "\n=== SCALABILITY SUMMARY ===\n\n";
    std::cout << "Comparing throughput scaling with thread count\n";
    std::cout << "(Mixed workload: 80% read, 20% write)\n\n";
    
    const int OPS_PER_THREAD = 50000;
    const size_t CACHE_SIZE = 100000;
    const int KEY_RANGE = 50000;
    
    std::cout << std::left << std::setw(20) << "Cache Type";
    for (int t : {1, 2, 4, 8, 16}) {
        std::cout << std::setw(12) << (std::to_string(t) + " thr");
    }
    std::cout << "\n" << std::string(80, '-') << "\n";
    
    auto formatThroughput = [](double opsPerSec) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(0) << (opsPerSec / 1000) << "K";
        return oss.str();
    };
    
    // ThreadSafeCache
    {
        std::cout << std::setw(20) << "ThreadSafeCache";
        for (int numThreads : {1, 2, 4, 8, 16}) {
            auto cache = makeThreadSafe(CACHE_SIZE);
            auto result = benchmarkMixed(cache, "", numThreads, OPS_PER_THREAD, KEY_RANGE);
            std::cout << std::setw(12) << formatThroughput(result.opsPerSec);
        }
        std::cout << "\n";
    }
    
    // ShardedCache variants
    for (const auto& [name, shards] : std::vector<std::pair<std::string, int>>{
        {"Sharded<4>", 4}, {"Sharded<8>", 8}, {"Sharded<16>", 16}, {"Sharded<32>", 32}}) {
        
        std::cout << std::setw(20) << name;
        for (int numThreads : {1, 2, 4, 8, 16}) {
            std::unique_ptr<ICache<int, int>> cache;
            if (shards == 4) cache = makeSharded<4>(CACHE_SIZE);
            else if (shards == 8) cache = makeSharded<8>(CACHE_SIZE);
            else if (shards == 16) cache = makeSharded<16>(CACHE_SIZE);
            else cache = makeSharded<32>(CACHE_SIZE);
            
            auto result = benchmarkMixed(cache, "", numThreads, OPS_PER_THREAD, KEY_RANGE);
            std::cout << std::setw(12) << formatThroughput(result.opsPerSec);
        }
        std::cout << "\n";
    }
}

int main(int argc, char* argv[]) {
    std::cout << "=== Cache Concurrency Benchmark ===\n";
    std::cout << "Hardware threads: " << std::thread::hardware_concurrency() << "\n";
    
    bool runAll = (argc == 1);
    std::string mode = (argc > 1) ? argv[1] : "";
    
    if (runAll || mode == "write") {
        runWriteBenchmark();
    }
    if (runAll || mode == "read") {
        runReadBenchmark();
    }
    if (runAll || mode == "mixed") {
        runMixedBenchmark();
    }
    if (runAll || mode == "hotkeys") {
        runHotKeysBenchmark();
    }
    if (runAll || mode == "scale") {
        runScalabilityTest();
    }
    
    std::cout << "\n=== Benchmark Complete ===\n";
    
    return 0;
}