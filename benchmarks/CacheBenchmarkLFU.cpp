#include <cache/Cache.hpp>
#include <cache/policies/LRUPolicy.hpp>
#include <cache/policies/LFUPolicy.hpp>
#include <cache/listeners/StatsListener.hpp>

#include <iostream>
#include <chrono>
#include <random>
#include <vector>
#include <string>
#include <iomanip>
#include <cmath>

/**
 * @brief Бенчмарк для сравнения LRU vs LFU политик вытеснения
 * 
 * Измеряем:
 * - Throughput операций put/get (ops/sec)
 * - Hit rate при разных паттернах доступа
 * - Накладные расходы на поддержание структур данных
 * 
 * Паттерны доступа:
 * - Uniform: равномерное распределение запросов
 * - Zipf: степенное распределение (80/20 правило)
 * - Temporal: временная локальность (недавние ключи чаще)
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
 * @brief Форматированный вывод результата бенчмарка
 */
void printResult(const std::string& name, double timeMs, size_t operations) {
    double opsPerSec = (operations / timeMs) * 1000.0;
    std::cout << std::left << std::setw(45) << name 
              << std::right << std::setw(10) << std::fixed << std::setprecision(2) 
              << timeMs << " ms"
              << std::setw(15) << std::fixed << std::setprecision(0) 
              << opsPerSec << " ops/sec\n";
}

/**
 * @brief Вывод сравнительной таблицы
 */
void printComparison(const std::string& testName,
                     double lruTimeMs, double lfuTimeMs,
                     double lruHitRate, double lfuHitRate) {
    std::cout << "\n--- " << testName << " ---\n";
    std::cout << std::left << std::setw(15) << "Metric"
              << std::setw(15) << "LRU"
              << std::setw(15) << "LFU"
              << std::setw(15) << "Winner\n";
    std::cout << std::string(60, '-') << "\n";
    
    // Время
    std::cout << std::left << std::setw(15) << "Time (ms)"
              << std::setw(15) << std::fixed << std::setprecision(2) << lruTimeMs
              << std::setw(15) << std::fixed << std::setprecision(2) << lfuTimeMs
              << std::setw(15) << (lruTimeMs < lfuTimeMs ? "LRU" : "LFU") << "\n";
    
    // Hit rate
    std::cout << std::left << std::setw(15) << "Hit Rate (%)"
              << std::setw(15) << std::fixed << std::setprecision(1) << (lruHitRate * 100)
              << std::setw(15) << std::fixed << std::setprecision(1) << (lfuHitRate * 100)
              << std::setw(15) << (lruHitRate > lfuHitRate ? "LRU" : "LFU") << "\n";
}

/**
 * @brief Генератор Zipf-распределения
 * 
 * Моделирует реальные паттерны доступа, где небольшое количество
 * элементов запрашивается очень часто (правило 80/20).
 * 
 * @param n Количество уникальных элементов
 * @param s Параметр скоса (1.0 = классический Zipf)
 */
class ZipfGenerator {
public:
    ZipfGenerator(size_t n, double s, uint32_t seed = 42)
        : n_(n), s_(s), rng_(seed), dist_(0.0, 1.0)
    {
        // Предвычисляем кумулятивные вероятности
        double sum = 0.0;
        for (size_t i = 1; i <= n; ++i) {
            sum += 1.0 / std::pow(static_cast<double>(i), s);
        }
        
        cumulative_.reserve(n);
        double cumSum = 0.0;
        for (size_t i = 1; i <= n; ++i) {
            cumSum += (1.0 / std::pow(static_cast<double>(i), s)) / sum;
            cumulative_.push_back(cumSum);
        }
    }
    
    size_t next() {
        double u = dist_(rng_);
        
        // Бинарный поиск по кумулятивным вероятностям
        auto it = std::lower_bound(cumulative_.begin(), cumulative_.end(), u);
        return std::distance(cumulative_.begin(), it);
    }
    
private:
    size_t n_;
    double s_;
    std::mt19937 rng_;
    std::uniform_real_distribution<double> dist_;
    std::vector<double> cumulative_;
};

// ==================== Бенчмарки ====================

/**
 * @brief Бенчмарк равномерного распределения (Uniform)
 * 
 * Каждый ключ имеет равную вероятность быть запрошенным.
 * LRU и LFU должны показывать похожие результаты.
 */
void benchmarkUniformAccess(size_t cacheSize, size_t keyRange, size_t numOperations) {
    std::cout << "\n=== Uniform Access Pattern ===\n";
    std::cout << "Cache size: " << cacheSize << ", Key range: " << keyRange 
              << ", Operations: " << numOperations << "\n\n";
    
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, static_cast<int>(keyRange - 1));
    
    // Предгенерируем ключи
    std::vector<int> keys(numOperations);
    for (size_t i = 0; i < numOperations; ++i) {
        keys[i] = dist(rng);
    }
    
    // LRU
    double lruTime, lruHitRate;
    {
        Cache<int, int> cache(cacheSize, std::make_unique<LRUPolicy<int>>());
        auto stats = std::make_shared<StatsListener<int, int>>();
        cache.addListener(stats);
        
        lruTime = measureMs([&]() {
            for (size_t i = 0; i < numOperations; ++i) {
                int key = keys[i];
                if (!cache.get(key).has_value()) {
                    cache.put(key, key * 10);
                }
            }
        });
        
        lruHitRate = stats->hitRate();
    }
    
    // LFU
    double lfuTime, lfuHitRate;
    {
        Cache<int, int> cache(cacheSize, std::make_unique<LFUPolicy<int>>());
        auto stats = std::make_shared<StatsListener<int, int>>();
        cache.addListener(stats);
        
        lfuTime = measureMs([&]() {
            for (size_t i = 0; i < numOperations; ++i) {
                int key = keys[i];
                if (!cache.get(key).has_value()) {
                    cache.put(key, key * 10);
                }
            }
        });
        
        lfuHitRate = stats->hitRate();
    }
    
    printComparison("Uniform Access", lruTime, lfuTime, lruHitRate, lfuHitRate);
}

/**
 * @brief Бенчмарк Zipf-распределения
 * 
 * Небольшое количество "горячих" ключей запрашивается очень часто.
 * LFU должен показывать лучший hit rate, т.к. сохраняет часто используемые элементы.
 */
void benchmarkZipfAccess(size_t cacheSize, size_t keyRange, size_t numOperations) {
    std::cout << "\n=== Zipf Access Pattern (s=1.0) ===\n";
    std::cout << "Cache size: " << cacheSize << ", Key range: " << keyRange 
              << ", Operations: " << numOperations << "\n\n";
    
    ZipfGenerator zipf(keyRange, 1.0);
    
    // Предгенерируем ключи
    std::vector<int> keys(numOperations);
    for (size_t i = 0; i < numOperations; ++i) {
        keys[i] = static_cast<int>(zipf.next());
    }
    
    // LRU
    double lruTime, lruHitRate;
    {
        Cache<int, int> cache(cacheSize, std::make_unique<LRUPolicy<int>>());
        auto stats = std::make_shared<StatsListener<int, int>>();
        cache.addListener(stats);
        
        lruTime = measureMs([&]() {
            for (size_t i = 0; i < numOperations; ++i) {
                int key = keys[i];
                if (!cache.get(key).has_value()) {
                    cache.put(key, key * 10);
                }
            }
        });
        
        lruHitRate = stats->hitRate();
    }
    
    // LFU
    double lfuTime, lfuHitRate;
    {
        Cache<int, int> cache(cacheSize, std::make_unique<LFUPolicy<int>>());
        auto stats = std::make_shared<StatsListener<int, int>>();
        cache.addListener(stats);
        
        lfuTime = measureMs([&]() {
            for (size_t i = 0; i < numOperations; ++i) {
                int key = keys[i];
                if (!cache.get(key).has_value()) {
                    cache.put(key, key * 10);
                }
            }
        });
        
        lfuHitRate = stats->hitRate();
    }
    
    printComparison("Zipf Access", lruTime, lfuTime, lruHitRate, lfuHitRate);
}

/**
 * @brief Бенчмарк временной локальности (Temporal Locality)
 * 
 * Недавно добавленные ключи чаще запрашиваются.
 * LRU должен показывать лучший hit rate в этом сценарии.
 */
void benchmarkTemporalLocality(size_t cacheSize, size_t numOperations) {
    std::cout << "\n=== Temporal Locality Pattern ===\n";
    std::cout << "Cache size: " << cacheSize 
              << ", Operations: " << numOperations << "\n\n";
    
    std::mt19937 rng(42);
    
    // Генерируем ключи с временной локальностью:
    // 70% запросов к последним 20% добавленных ключей
    std::vector<std::pair<bool, int>> operations;  // {is_new, key}
    operations.reserve(numOperations);
    
    int nextNewKey = 0;
    std::vector<int> recentKeys;  // Последние добавленные ключи
    
    for (size_t i = 0; i < numOperations; ++i) {
        std::uniform_int_distribution<int> opDist(0, 99);
        int op = opDist(rng);
        
        if (op < 30 || recentKeys.empty()) {
            // 30% — новый ключ
            operations.push_back({true, nextNewKey});
            recentKeys.push_back(nextNewKey);
            if (recentKeys.size() > cacheSize / 5) {
                recentKeys.erase(recentKeys.begin());
            }
            ++nextNewKey;
        } else {
            // 70% — запрос к недавнему ключу
            std::uniform_int_distribution<size_t> keyDist(0, recentKeys.size() - 1);
            operations.push_back({false, recentKeys[keyDist(rng)]});
        }
    }
    
    // LRU
    double lruTime, lruHitRate;
    {
        Cache<int, int> cache(cacheSize, std::make_unique<LRUPolicy<int>>());
        auto stats = std::make_shared<StatsListener<int, int>>();
        cache.addListener(stats);
        
        lruTime = measureMs([&]() {
            for (const auto& op : operations) {
                if (op.first) {
                    cache.put(op.second, op.second * 10);
                } else {
                    cache.get(op.second);
                }
            }
        });
        
        lruHitRate = stats->hitRate();
    }
    
    // LFU
    double lfuTime, lfuHitRate;
    {
        Cache<int, int> cache(cacheSize, std::make_unique<LFUPolicy<int>>());
        auto stats = std::make_shared<StatsListener<int, int>>();
        cache.addListener(stats);
        
        lfuTime = measureMs([&]() {
            for (const auto& op : operations) {
                if (op.first) {
                    cache.put(op.second, op.second * 10);
                } else {
                    cache.get(op.second);
                }
            }
        });
        
        lfuHitRate = stats->hitRate();
    }
    
    printComparison("Temporal Locality", lruTime, lfuTime, lruHitRate, lfuHitRate);
}

/**
 * @brief Бенчмарк смены рабочего набора (Working Set Shift)
 * 
 * Рабочий набор меняется со временем.
 * LRU адаптируется быстрее, LFU удерживает старые "горячие" данные.
 */
void benchmarkWorkingSetShift(size_t cacheSize, size_t numOperations) {
    std::cout << "\n=== Working Set Shift Pattern ===\n";
    std::cout << "Cache size: " << cacheSize 
              << ", Operations: " << numOperations << "\n\n";
    
    std::mt19937 rng(42);
    
    // Фаза 1: Ключи 0-99
    // Фаза 2: Ключи 100-199
    // Фаза 3: Ключи 200-299
    size_t phaseSize = numOperations / 3;
    
    std::vector<int> keys;
    keys.reserve(numOperations);
    
    for (int phase = 0; phase < 3; ++phase) {
        int baseKey = phase * 100;
        std::uniform_int_distribution<int> dist(baseKey, baseKey + 99);
        
        for (size_t i = 0; i < phaseSize; ++i) {
            keys.push_back(dist(rng));
        }
    }
    
    // LRU
    double lruTime, lruHitRate;
    {
        Cache<int, int> cache(cacheSize, std::make_unique<LRUPolicy<int>>());
        auto stats = std::make_shared<StatsListener<int, int>>();
        cache.addListener(stats);
        
        lruTime = measureMs([&]() {
            for (int key : keys) {
                if (!cache.get(key).has_value()) {
                    cache.put(key, key * 10);
                }
            }
        });
        
        lruHitRate = stats->hitRate();
    }
    
    // LFU
    double lfuTime, lfuHitRate;
    {
        Cache<int, int> cache(cacheSize, std::make_unique<LFUPolicy<int>>());
        auto stats = std::make_shared<StatsListener<int, int>>();
        cache.addListener(stats);
        
        lfuTime = measureMs([&]() {
            for (int key : keys) {
                if (!cache.get(key).has_value()) {
                    cache.put(key, key * 10);
                }
            }
        });
        
        lfuHitRate = stats->hitRate();
    }
    
    printComparison("Working Set Shift", lruTime, lfuTime, lruHitRate, lfuHitRate);
}

/**
 * @brief Бенчмарк чистой производительности put()
 */
void benchmarkPutPerformance(size_t cacheSize, size_t numOperations) {
    std::cout << "\n=== Pure Put Performance ===\n";
    
    // LRU
    {
        Cache<int, int> cache(cacheSize, std::make_unique<LRUPolicy<int>>());
        
        double timeMs = measureMs([&]() {
            for (size_t i = 0; i < numOperations; ++i) {
                cache.put(static_cast<int>(i), static_cast<int>(i * 10));
            }
        });
        
        printResult("LRU put (with evictions)", timeMs, numOperations);
    }
    
    // LFU
    {
        Cache<int, int> cache(cacheSize, std::make_unique<LFUPolicy<int>>());
        
        double timeMs = measureMs([&]() {
            for (size_t i = 0; i < numOperations; ++i) {
                cache.put(static_cast<int>(i), static_cast<int>(i * 10));
            }
        });
        
        printResult("LFU put (with evictions)", timeMs, numOperations);
    }
}

/**
 * @brief Бенчмарк чистой производительности get()
 */
void benchmarkGetPerformance(size_t cacheSize, size_t numOperations) {
    std::cout << "\n=== Pure Get Performance (100% hit) ===\n";
    
    // LRU
    {
        Cache<int, int> cache(cacheSize, std::make_unique<LRUPolicy<int>>());
        
        // Заполняем кэш
        for (size_t i = 0; i < cacheSize; ++i) {
            cache.put(static_cast<int>(i), static_cast<int>(i));
        }
        
        double timeMs = measureMs([&]() {
            for (size_t i = 0; i < numOperations; ++i) {
                cache.get(static_cast<int>(i % cacheSize));
            }
        });
        
        printResult("LRU get (100% hit)", timeMs, numOperations);
    }
    
    // LFU
    {
        Cache<int, int> cache(cacheSize, std::make_unique<LFUPolicy<int>>());
        
        // Заполняем кэш
        for (size_t i = 0; i < cacheSize; ++i) {
            cache.put(static_cast<int>(i), static_cast<int>(i));
        }
        
        double timeMs = measureMs([&]() {
            for (size_t i = 0; i < numOperations; ++i) {
                cache.get(static_cast<int>(i % cacheSize));
            }
        });
        
        printResult("LFU get (100% hit)", timeMs, numOperations);
    }
}

// ==================== Main ====================

int main() {
    const size_t CACHE_SIZE = 1000;
    const size_t KEY_RANGE = 10000;
    const size_t NUM_OPS = 500000;
    
    std::cout << "========================================\n";
    std::cout << "     LRU vs LFU Benchmark Suite\n";
    std::cout << "========================================\n";
    
    // Производительность
    benchmarkPutPerformance(CACHE_SIZE, NUM_OPS);
    benchmarkGetPerformance(CACHE_SIZE, NUM_OPS);
    
    // Hit rate при разных паттернах
    benchmarkUniformAccess(CACHE_SIZE, KEY_RANGE, NUM_OPS);
    benchmarkZipfAccess(CACHE_SIZE, KEY_RANGE, NUM_OPS);
    benchmarkTemporalLocality(CACHE_SIZE, NUM_OPS);
    benchmarkWorkingSetShift(CACHE_SIZE, NUM_OPS);
    
    std::cout << "\n========================================\n";
    std::cout << "              Summary\n";
    std::cout << "========================================\n";
    std::cout << "\nLRU лучше подходит для:\n";
    std::cout << "  - Временная локальность (недавние данные важнее)\n";
    std::cout << "  - Смена рабочего набора (быстрая адаптация)\n";
    std::cout << "  - Простая реализация и предсказуемость\n";
    
    std::cout << "\nLFU лучше подходит для:\n";
    std::cout << "  - Zipf-распределение (есть явно \"горячие\" данные)\n";
    std::cout << "  - Стабильный рабочий набор\n";
    std::cout << "  - Когда частота важнее недавности\n";
    
    std::cout << "\n========================================\n";
    std::cout << "         Benchmark Complete\n";
    std::cout << "========================================\n";
    
    return 0;
}