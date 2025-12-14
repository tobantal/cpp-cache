#include <cache/Cache.hpp>
#include <cache/policies/LRUPolicy.hpp>
#include <cache/listeners/StatsListener.hpp>

#include <iostream>
#include <chrono>
#include <random>
#include <vector>
#include <string>
#include <iomanip>

/**
 * @brief Простой бенчмарк для кэша
 * 
 * Измеряем:
 * - Throughput операций put/get (ops/sec)
 * - Влияние слушателей на производительность
 * - Hit rate при разных паттернах доступа
 * 
 * Без внешних зависимостей — используем std::chrono для замеров.
 */

// ==================== Утилиты ====================

/**
 * @brief Замер времени выполнения функции
 * @return Время в миллисекундах
 */
template<typename Func>
double measureMs(Func&& func) {
    auto start = std::chrono::high_resolution_clock::now();
    func();
    auto end = std::chrono::high_resolution_clock::now();
    
    std::chrono::duration<double, std::milli> duration = end - start;
    return duration.count();
}

/**
 * @brief Форматированный вывод результата
 */
void printResult(const std::string& name, double timeMs, size_t operations) {
    double opsPerSec = (operations / timeMs) * 1000.0;
    std::cout << std::left << std::setw(40) << name 
              << std::right << std::setw(10) << std::fixed << std::setprecision(2) 
              << timeMs << " ms"
              << std::setw(15) << std::fixed << std::setprecision(0) 
              << opsPerSec << " ops/sec\n";
}

/**
 * @brief Создаёт LRU кэш заданной ёмкости
 */
template<typename K, typename V>
Cache<K, V> makeLRUCache(size_t capacity) {
    return Cache<K, V>(capacity, std::make_unique<LRUPolicy<K>>());
}

// ==================== Бенчмарки ====================

/**
 * @brief Бенчмарк последовательной вставки
 * 
 * Вставляем N элементов в пустой кэш.
 * Кэш меньше N — будут вытеснения.
 */
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

/**
 * @brief Бенчмарк последовательного чтения
 * 
 * Читаем существующие ключи — 100% hit rate.
 */
void benchmarkSequentialGet(size_t cacheSize, size_t numOperations) {
    auto cache = makeLRUCache<int, int>(cacheSize);
    
    // Заполняем кэш
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

/**
 * @brief Бенчмарк случайного доступа
 * 
 * Случайные ключи из диапазона [0, keyRange).
 * Если keyRange > cacheSize — будут промахи.
 */
void benchmarkRandomAccess(size_t cacheSize, size_t numOperations, size_t keyRange) {
    auto cache = makeLRUCache<int, int>(cacheSize);
    auto stats = std::make_shared<StatsListener<int, int>>();
    cache.addListener(stats);
    
    // Генератор случайных чисел
    std::mt19937 rng(42);  // Фиксированный seed для воспроизводимости
    std::uniform_int_distribution<int> dist(0, static_cast<int>(keyRange - 1));
    
    // Предгенерируем ключи чтобы не мерять время генерации
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

/**
 * @brief Бенчмарк смешанной нагрузки
 * 
 * 80% чтений, 20% записей — типичный паттерн.
 */
void benchmarkMixedWorkload(size_t cacheSize, size_t numOperations) {
    auto cache = makeLRUCache<int, int>(cacheSize);
    auto stats = std::make_shared<StatsListener<int, int>>();
    cache.addListener(stats);
    
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> keyDist(0, static_cast<int>(cacheSize * 2));
    std::uniform_int_distribution<int> opDist(0, 99);
    
    // Предгенерируем операции
    std::vector<std::pair<int, int>> operations(numOperations);  // {key, op_type}
    for (size_t i = 0; i < numOperations; ++i) {
        operations[i] = {keyDist(rng), opDist(rng)};
    }
    
    double timeMs = measureMs([&]() {
        for (size_t i = 0; i < numOperations; ++i) {
            int key = operations[i].first;
            int op = operations[i].second;
            
            if (op < 80) {
                // 80% — чтение
                cache.get(key);
            } else {
                // 20% — запись
                cache.put(key, key * 10);
            }
        }
    });
    
    printResult("Mixed workload (80% read, 20% write)", timeMs, numOperations);
    std::cout << "   Hit rate: " << std::fixed << std::setprecision(2) 
              << (stats->hitRate() * 100) << "%\n";
}

/**
 * @brief Сравнение производительности с/без слушателей
 * 
 * Проверяем оверхед от Observer pattern.
 */
void benchmarkListenerOverhead(size_t cacheSize, size_t numOperations) {
    std::cout << "\n--- Listener overhead ---\n";
    
    // Без слушателей
    {
        auto cache = makeLRUCache<int, int>(cacheSize);
        
        double timeMs = measureMs([&]() {
            for (size_t i = 0; i < numOperations; ++i) {
                cache.put(static_cast<int>(i % cacheSize), static_cast<int>(i));
                cache.get(static_cast<int>(i % cacheSize));
            }
        });
        
        printResult("Without listeners", timeMs, numOperations * 2);
    }
    
    // С одним слушателем
    {
        auto cache = makeLRUCache<int, int>(cacheSize);
        auto stats = std::make_shared<StatsListener<int, int>>();
        cache.addListener(stats);
        
        double timeMs = measureMs([&]() {
            for (size_t i = 0; i < numOperations; ++i) {
                cache.put(static_cast<int>(i % cacheSize), static_cast<int>(i));
                cache.get(static_cast<int>(i % cacheSize));
            }
        });
        
        printResult("With StatsListener", timeMs, numOperations * 2);
    }
}

/**
 * @brief Бенчмарк вытеснений
 * 
 * Маленький кэш + много уникальных ключей = постоянные вытеснения.
 * Показывает накладные расходы на eviction.
 */
void benchmarkEvictionHeavy(size_t cacheSize, size_t numOperations) {
    auto cache = makeLRUCache<int, int>(cacheSize);
    auto stats = std::make_shared<StatsListener<int, int>>();
    cache.addListener(stats);
    
    double timeMs = measureMs([&]() {
        for (size_t i = 0; i < numOperations; ++i) {
            // Всегда уникальные ключи — каждая вставка вызывает вытеснение
            cache.put(static_cast<int>(i), static_cast<int>(i));
        }
    });
    
    printResult("Eviction-heavy (unique keys)", timeMs, numOperations);
    std::cout << "   Evictions: " << stats->evictions() << "\n";
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
    benchmarkRandomAccess(SMALL_CACHE, NUM_OPS, SMALL_CACHE);      // 100% в кэше
    benchmarkRandomAccess(SMALL_CACHE, NUM_OPS, SMALL_CACHE * 10); // 10% в кэше
    benchmarkMixedWorkload(LARGE_CACHE, NUM_OPS);
    
    benchmarkListenerOverhead(LARGE_CACHE, NUM_OPS / 2);
    
    std::cout << "\n--- Eviction stress test ---\n";
    benchmarkEvictionHeavy(SMALL_CACHE, NUM_OPS);
    
    std::cout << "\n=== Benchmark complete ===\n";
    
    return 0;
}