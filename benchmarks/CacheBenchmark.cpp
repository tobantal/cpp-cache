#include <cache/Cache.hpp>
#include <cache/listeners/ICacheListener.hpp>
#include <cache/eviction/LRUPolicy.hpp>
#include <cache/listeners/StatsListener.hpp>
#include <cache/listeners/LoggingListener.hpp>
#include <cache/listeners/ThreadPerListenerComposite.hpp>

#include <iostream>
#include <sstream>
#include <chrono>
#include <random>
#include <vector>
#include <string>
#include <iomanip>
#include <atomic>

/**
 * @brief Бенчмарк для кэша
 * 
 * Измеряем:
 * - Throughput операций put/get (ops/sec)
 * - Влияние слушателей на производительность
 * - Сравнение sync vs async слушателей
 * - Hit rate при разных паттернах доступа
 */

// ==================== Утилиты ====================

template<typename Func>
double measureMs(Func&& func) {
    auto start = std::chrono::high_resolution_clock::now();
    func();
    auto end = std::chrono::high_resolution_clock::now();
    
    std::chrono::duration<double, std::milli> duration = end - start;
    return duration.count();
}

void printResult(const std::string& name, double timeMs, size_t operations) {
    double opsPerSec = (operations / timeMs) * 1000.0;
    std::cout << std::left << std::setw(45) << name 
              << std::right << std::setw(10) << std::fixed << std::setprecision(2) 
              << timeMs << " ms"
              << std::setw(15) << std::fixed << std::setprecision(0) 
              << opsPerSec << " ops/sec\n";
}

template<typename K, typename V>
Cache<K, V> makeLRUCache(size_t capacity) {
    return Cache<K, V>(capacity, std::make_unique<LRUPolicy<K>>());
}

// ==================== Базовые бенчмарки ====================

void benchmarkSequentialPut(size_t cacheSize, size_t numOperations) {
    auto cache = makeLRUCache<int, int>(cacheSize);
    
    double timeMs = measureMs([&]() {
        for (size_t i = 0; i < numOperations; ++i) {
            cache.put(static_cast<int>(i), static_cast<int>(i * 10));
        }
    });
    
    printResult("Sequential put (size=" + std::to_string(cacheSize) + ")", 
                timeMs, numOperations);
}

void benchmarkSequentialGet(size_t cacheSize, size_t numOperations) {
    auto cache = makeLRUCache<int, int>(cacheSize);
    
    for (size_t i = 0; i < cacheSize; ++i) {
        cache.put(static_cast<int>(i), static_cast<int>(i));
    }
    
    double timeMs = measureMs([&]() {
        for (size_t i = 0; i < numOperations; ++i) {
            cache.get(static_cast<int>(i % cacheSize));
        }
    });
    
    printResult("Sequential get (100% hit)", timeMs, numOperations);
}

void benchmarkRandomAccess(size_t cacheSize, size_t numOperations, size_t keyRange) {
    auto cache = makeLRUCache<int, int>(cacheSize);
    auto stats = std::make_shared<StatsListener<int, int>>();
    cache.addListener(stats);
    
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, static_cast<int>(keyRange - 1));
    
    std::vector<int> keys(numOperations);
    for (size_t i = 0; i < numOperations; ++i) {
        keys[i] = dist(rng);
    }
    
    double timeMs = measureMs([&]() {
        for (size_t i = 0; i < numOperations; ++i) {
            int key = keys[i];
            if (!cache.get(key).has_value()) {
                cache.put(key, key * 10);
            }
        }
    });
    
    std::string name = "Random access (range=" + std::to_string(keyRange) + ")";
    printResult(name, timeMs, numOperations);
    
    std::cout << "   Hit rate: " << std::fixed << std::setprecision(2) 
              << (stats->hitRate() * 100) << "%\n";
}

void benchmarkMixedWorkload(size_t cacheSize, size_t numOperations) {
    auto cache = makeLRUCache<int, int>(cacheSize);
    auto stats = std::make_shared<StatsListener<int, int>>();
    cache.addListener(stats);
    
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> keyDist(0, static_cast<int>(cacheSize * 2));
    std::uniform_int_distribution<int> opDist(0, 99);
    
    std::vector<std::pair<int, int>> operations(numOperations);
    for (size_t i = 0; i < numOperations; ++i) {
        operations[i] = {keyDist(rng), opDist(rng)};
    }
    
    double timeMs = measureMs([&]() {
        for (size_t i = 0; i < numOperations; ++i) {
            int key = operations[i].first;
            int op = operations[i].second;
            
            if (op < 80) {
                cache.get(key);
            } else {
                cache.put(key, key * 10);
            }
        }
    });
    
    printResult("Mixed workload (80% read, 20% write)", timeMs, numOperations);
    std::cout << "   Hit rate: " << std::fixed << std::setprecision(2) 
              << (stats->hitRate() * 100) << "%\n";
}

void benchmarkEvictionHeavy(size_t cacheSize, size_t numOperations) {
    auto cache = makeLRUCache<int, int>(cacheSize);
    auto stats = std::make_shared<StatsListener<int, int>>();
    cache.addListener(stats);
    
    double timeMs = measureMs([&]() {
        for (size_t i = 0; i < numOperations; ++i) {
            cache.put(static_cast<int>(i), static_cast<int>(i));
        }
    });
    
    printResult("Eviction-heavy (unique keys)", timeMs, numOperations);
    std::cout << "   Evictions: " << stats->evictions() << "\n";
}

// ==================== Ключевой бенчмарк: Sync vs Async ====================

/**
 * @brief "Медленный" слушатель — симулирует I/O операции
 * 
 * Реальные слушатели могут делать:
 * - Запись в файл с fsync
 * - Отправка по сети
 * - Запись в БД
 * 
 * Симулируем busy wait (более честно чем sleep).
 */
template<typename K, typename V>
class SlowListener : public ICacheListener<K, V> {
public:
    explicit SlowListener(std::chrono::microseconds delay) : delay_(delay) {}
    
    void onHit(const K&) override { doWork(); }
    void onMiss(const K&) override { doWork(); }
    void onInsert(const K&, const V&) override { doWork(); }
    void onUpdate(const K&, const V&, const V&) override { doWork(); }
    void onEvict(const K&, const V&) override { doWork(); }
    void onRemove(const K&) override { doWork(); }
    void onClear(size_t) override { doWork(); }
    
    std::atomic<uint64_t> callCount{0};
    
private:
    void doWork() {
        ++callCount;
        // Busy wait — более предсказуемо чем sleep
        auto start = std::chrono::high_resolution_clock::now();
        while (std::chrono::high_resolution_clock::now() - start < delay_) {
            // spin
        }
    }
    
    std::chrono::microseconds delay_;
};

/**
 * @brief Сравнение производительности слушателей
 * 
 * Два сценария:
 * 1. ЛЁГКИЕ слушатели (Stats) — sync быстрее
 * 2. ТЯЖЁЛЫЕ слушатели (симуляция I/O) — async быстрее
 */
void benchmarkListenerOverhead(size_t cacheSize, size_t numOperations) {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "  LISTENER OVERHEAD: Sync vs Async\n";
    std::cout << "============================================================\n";
    
    // Подготовка ключей
    std::vector<int> keys(numOperations);
    for (size_t i = 0; i < numOperations; ++i) {
        keys[i] = static_cast<int>(i % cacheSize);
    }
    
    // ==================== Тест 1: Лёгкие слушатели ====================
    std::cout << "\n--- Test 1: LIGHTWEIGHT listeners (StatsListener) ---\n\n";
    std::cout << "  Operations: " << numOperations * 2 << " (put + get)\n\n";
    
    double baselineTime1 = 0;
    double syncTime1 = 0;
    double asyncTime1 = 0;
    
    // Baseline
    {
        auto cache = makeLRUCache<int, int>(cacheSize);
        baselineTime1 = measureMs([&]() {
            for (size_t i = 0; i < numOperations; ++i) {
                cache.put(keys[i], static_cast<int>(i));
                cache.get(keys[i]);
            }
        });
        printResult("  Baseline (no listeners)", baselineTime1, numOperations * 2);
    }
    
    // Sync
    {
        auto cache = makeLRUCache<int, int>(cacheSize);
        auto stats = std::make_shared<StatsListener<int, int>>();
        cache.addListener(stats);
        
        syncTime1 = measureMs([&]() {
            for (size_t i = 0; i < numOperations; ++i) {
                cache.put(keys[i], static_cast<int>(i));
                cache.get(keys[i]);
            }
        });
        printResult("  SYNC StatsListener", syncTime1, numOperations * 2);
    }
    
    // Async
    {
        auto cache = makeLRUCache<int, int>(cacheSize);
        auto composite = std::make_shared<ThreadPerListenerComposite<int, int>>();
        auto stats = std::make_shared<StatsListener<int, int>>();
        composite->addListener(stats);
        cache.addListener(composite);
        
        asyncTime1 = measureMs([&]() {
            for (size_t i = 0; i < numOperations; ++i) {
                cache.put(keys[i], static_cast<int>(i));
                cache.get(keys[i]);
            }
        });
        printResult("  ASYNC StatsListener", asyncTime1, numOperations * 2);
        composite->stop();
    }
    
    double syncOverhead1 = ((syncTime1 - baselineTime1) / baselineTime1) * 100;
    double asyncOverhead1 = ((asyncTime1 - baselineTime1) / baselineTime1) * 100;
    
    std::cout << "\n  Result: Sync overhead +" << std::fixed << std::setprecision(1) 
              << syncOverhead1 << "%, Async overhead +" << asyncOverhead1 << "%\n";
    std::cout << "  → For lightweight listeners, SYNC is faster (no queue overhead)\n";
    
    // ==================== Тест 2: Тяжёлые слушатели ====================
    size_t heavyOps = numOperations / 10;  // Меньше операций для тяжёлого теста
    
    std::cout << "\n--- Test 2: HEAVY listeners (simulated 10μs I/O per event) ---\n\n";
    std::cout << "  Operations: " << heavyOps << " (put only)\n\n";
    
    double baselineTime2 = 0;
    double syncTime2 = 0;
    double asyncTime2 = 0;
    
    // Baseline
    {
        auto cache = makeLRUCache<int, int>(cacheSize);
        baselineTime2 = measureMs([&]() {
            for (size_t i = 0; i < heavyOps; ++i) {
                cache.put(keys[i], static_cast<int>(i));
            }
        });
        printResult("  Baseline (no listeners)", baselineTime2, heavyOps);
    }
    
    // Sync с медленным слушателем
    {
        auto cache = makeLRUCache<int, int>(cacheSize);
        auto slow = std::make_shared<SlowListener<int, int>>(std::chrono::microseconds(10));
        cache.addListener(slow);
        
        syncTime2 = measureMs([&]() {
            for (size_t i = 0; i < heavyOps; ++i) {
                cache.put(keys[i], static_cast<int>(i));
            }
        });
        printResult("  SYNC SlowListener (10μs/event)", syncTime2, heavyOps);
    }
    
    // Async с медленным слушателем
    {
        auto cache = makeLRUCache<int, int>(cacheSize);
        auto composite = std::make_shared<ThreadPerListenerComposite<int, int>>();
        auto slow = std::make_shared<SlowListener<int, int>>(std::chrono::microseconds(10));
        composite->addListener(slow);
        cache.addListener(composite);
        
        asyncTime2 = measureMs([&]() {
            for (size_t i = 0; i < heavyOps; ++i) {
                cache.put(keys[i], static_cast<int>(i));
            }
        });
        
        printResult("  ASYNC SlowListener (10μs/event)", asyncTime2, heavyOps);
        
        double drainTime = measureMs([&]() {
            composite->stop();
        });
        std::cout << "     Background drain: " << std::fixed << std::setprecision(0) 
                  << drainTime << " ms\n";
    }
    
    double syncOverhead2 = ((syncTime2 - baselineTime2) / baselineTime2) * 100;
    double asyncOverhead2 = ((asyncTime2 - baselineTime2) / baselineTime2) * 100;
    double speedup = syncTime2 / asyncTime2;
    
    std::cout << "\n  Result: Sync overhead +" << std::fixed << std::setprecision(0) 
              << syncOverhead2 << "%, Async overhead +" << asyncOverhead2 << "%\n";
    std::cout << "  → ASYNC is " << std::setprecision(1) << speedup << "x faster!\n";
    
    // ==================== Итоги ====================
    std::cout << "\n============================================================\n";
    std::cout << "SUMMARY:\n\n";
    std::cout << "  • Lightweight listeners: use SYNC (direct callbacks)\n";
    std::cout << "  • Heavy listeners (I/O, persistence): use ASYNC\n";
    std::cout << "  • Mixed: wrap only heavy listeners in Composite\n";
    std::cout << "============================================================\n";
}

// ==================== Main ====================

int main() {
    const size_t SMALL_CACHE = 1000;
    const size_t LARGE_CACHE = 100000;
    const size_t NUM_OPS = 1000000;
    
    std::cout << "=== Cache Benchmark ===\n";
    std::cout << "Operations: " << NUM_OPS << "\n\n";
    
    std::cout << "--- Basic operations ---\n";
    benchmarkSequentialPut(SMALL_CACHE, NUM_OPS);
    benchmarkSequentialPut(LARGE_CACHE, NUM_OPS);
    benchmarkSequentialGet(LARGE_CACHE, NUM_OPS);
    
    std::cout << "\n--- Access patterns ---\n";
    benchmarkRandomAccess(SMALL_CACHE, NUM_OPS, SMALL_CACHE);
    benchmarkRandomAccess(SMALL_CACHE, NUM_OPS, SMALL_CACHE * 10);
    benchmarkMixedWorkload(LARGE_CACHE, NUM_OPS);
    
    // Ключевой бенчмарк — sync vs async listeners
    benchmarkListenerOverhead(LARGE_CACHE, NUM_OPS / 2);
    
    std::cout << "\n--- Eviction stress test ---\n";
    benchmarkEvictionHeavy(SMALL_CACHE, NUM_OPS);
    
    std::cout << "\n=== Benchmark complete ===\n";
    
    return 0;
}