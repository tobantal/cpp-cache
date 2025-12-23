#include <iostream>
#include <chrono>
#include <iomanip>
#include <vector>
#include <memory>

#include "BenchmarkConfig.hpp"
#include "strategies/CacheStrategy.hpp"
#include "strategies/OurCacheStrategy.hpp"
#include "strategies/LRUCache11Strategy.hpp"
#include "strategies/CppLRUStrategy.hpp"
#include "workloads/Workload.hpp"
#include "workloads/ZipfWorkload.hpp"
#include "workloads/TemporalWorkload.hpp"


/**
 * @brief Измеряет время выполнения функции в миллисекундах
 */
template<typename Func>
double measureMs(Func&& func) {
    auto start = std::chrono::high_resolution_clock::now();
    func();
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}


/**
 * @brief Красивый вывод результата
 */
void printResult(const std::string& name, double timeMs, size_t operations, double hitRate = -1.0) {
    double opsPerSec = (operations / timeMs) * 1000.0;
    
    std::cout << std::left << std::setw(50) << name
              << std::right << std::setw(12) << std::fixed << std::setprecision(2) << timeMs << " ms"
              << std::setw(15) << std::fixed << std::setprecision(0) << opsPerSec << " ops/s";
    
    if (hitRate >= 0.0) {
        std::cout << std::setw(12) << std::fixed << std::setprecision(1) << (hitRate * 100.0) << "%";
    }
    
    std::cout << "\n";
}


/**
 * @brief Заголовок таблицы с описанием теста
 */
void printHeader(const std::string& title, const std::string& description = "") {
    std::cout << "\n";
    std::cout << "=== " << title << " ===\n";
    if (!description.empty()) {
        std::cout << description << "\n";
    }
    std::cout << std::left << std::setw(50) << "Test"
              << std::right << std::setw(12) << "Time"
              << std::setw(15) << "Throughput"
              << std::setw(12) << "Hit Rate\n";
    std::cout << std::string(80, '-') << "\n";
}


/**
 * ============================================================================
 * CRITERION 1: SEQUENTIAL PUT (Write-Heavy Baseline)
 * ============================================================================
 * 
 * @brief Тестирует чистую производительность операций добавления
 * 
 * Сценарий:
 * - Добавляем 1M элементов последовательно (key = 0, 1, 2, ...)
 * - Key range (200K) > cache size (100K) → постоянно вытесняются элементы
 * - Это самый "худший" сценарий для кэша
 * 
 * Что измеряем:
 * - Чистая скорость write операций
 * - Влияние overhead'а архитектуры кэша
 * - Производительность функции evict() (для каждого PUT после заполнения)
 * 
 * Почему это важно:
 * - Write-heavy приложения (логирование, очереди, временные ряды)
 * - Базовая линия для сравнения
 * - Показывает архитектурный overhead
 * 
 * Ожидаемый результат:
 * - cpp-lru-cache: ~60-65ms (минимум функциональности)
 * - LRUCache11: ~70-80ms (простая реализация)
 * - OurCache: ~130-140ms (thread-safe + TTL + listeners)
 * - Hit rate не имеет значения (постоянно вытесняются элементы)
 */
void testSequentialPUT(CacheStrategy<int, int>& strategy,
                       const BenchmarkConfig& config) {
    strategy.clear();
    
    double timeMs = measureMs([&]() {
        for (size_t i = 0; i < config.num_operations; ++i) {
            strategy.put(static_cast<int>(i), static_cast<int>(i * 10));
        }
    });
    
    printResult(strategy.name() + " - Sequential PUT", timeMs, config.num_operations);
}


/**
 * ============================================================================
 * CRITERION 2: SEQUENTIAL GET (Read-Heavy with 100% Hit Rate)
 * ============================================================================
 * 
 * @brief Тестирует производительность чтения в идеальных условиях
 * 
 * Сценарий:
 * - Сначала заполняем кэш ровно cache_size элементами (100K)
 * - Потом читаем эти же 100K элементов 10 раз (1M GET операций)
 * - ВСЕ операции попадают в кэш (100% hit rate)
 * 
 * Что измеряем:
 * - Чистая скорость чтения без промахов
 * - Скорость обновления LRU метаданных (для каждого GET)
 * - Minimal cache behavior
 * 
 * Почему это важно:
 * - Read-heavy приложения (веб-кэш, CDN, сессии)
 * - Лучший сценарий для кэша
 * - Показывает базовую скорость поиска
 * 
 * Ожидаемый результат:
 * - cpp-lru-cache: ~16-18ms (50M+ ops/sec)
 * - LRUCache11: ~17-19ms (50M+ ops/sec)
 * - OurCache: ~19-21ms (50M+ ops/sec)
 * - Hit rate: 100% (все элементы в кэше)
 */
void testSequentialGET(CacheStrategy<int, int>& strategy, const BenchmarkConfig config) {
    strategy.clear();
    
    for (size_t i = 0; i < config.cache_size; i++) {
        strategy.put(i, i);
    }
    
    size_t hits = 0;
    size_t totalgets = 0;
    
    double timeMs = measureMs([&]() {
        for (size_t op = 0; op < config.num_operations; op++) {
            int key = op % config.cache_size;  // Циклируем по ключам
            auto result = strategy.get(key);
            if (result.has_value()) hits++;
            totalgets++;
        }
    });
    
    double hitRate = totalgets > 0 ? (double)hits / totalgets * 100.0 : 0.0;
    printResult(strategy.name() + " - Sequential GET", timeMs, totalgets, hitRate);
}


/**
 * ============================================================================
 * CRITERION 3: MIXED 80/20 WORKLOAD (Balanced Real-World Scenario)
 * ============================================================================
 * 
 * @brief Тестирует реалистичный сценарий с смешанными операциями
 * 
 * Сценарий:
 * - 80% операций — GET (чтение)
 * - 20% операций — PUT (запись)
 * - Uniform распределение ключей (все ключи равновероятны)
 * - Key range (200K) > cache size (100K)
 * 
 * Что измеряем:
 * - Реальная производительность при смешанной нагрузке
 * - Hit rate при случайном доступе
 * - Балансировка между чтением и записью
 * 
 * Почему это важно:
 * - Типичное приложение (веб-сервер, БД кэш, меркле)
 * - Тестирует реальное поведение
 * - Показывает, как политика вытеснения работает на практике
 * 
 * Ожидаемый результат:
 * - Hit rate: ~50% (половина ключей "свежие", половина вытеснены)
 * - OurCache: 5-8M ops/sec (медленнее из-за overhead)
 * - LRUCache11/cpp-lru: 1.5-2M ops/sec (медленнее чем PUT из-за случайности)
 * 
 * Почему медленнее чем Criterion 1?
 * - Случайный доступ хуже для кэша процессора
 * - Mix read/write операций
 */
void testMixed8020(CacheStrategy<int, int>& strategy,
                   const BenchmarkConfig& config) {
    strategy.clear();
    
    // Заполняем кэш начальными данными
    for (size_t i = 0; i < config.cache_size; ++i) {
        strategy.put(static_cast<int>(i), static_cast<int>(i * 10));
    }
    
    size_t total_ops = config.num_operations;
    size_t read_ops = 0;
    
    std::mt19937 rng(config.random_seed);
    std::uniform_int_distribution<int> key_dist(0, static_cast<int>(config.getKeyRange() - 1));
    std::uniform_real_distribution<double> op_dist(0.0, 1.0);
    
    size_t hits = 0;
    
    double timeMs = measureMs([&]() {
        for (size_t i = 0; i < total_ops; ++i) {
            if (op_dist(rng) < 0.8) {
                // GET operation (80%)
                int key = key_dist(rng);
                auto result = strategy.get(key);
                if (result.has_value()) {
                    hits++;
                }
                read_ops++;
            } else {
                // PUT operation (20%)
                int key = key_dist(rng);
                int value = static_cast<int>(key * 10);
                strategy.put(key, value);
            }
        }
    });
    
    double hitRate = read_ops > 0 ? static_cast<double>(hits) / read_ops : 0.0;
    printResult(strategy.name() + " - Mixed 80/20 (Uniform)", timeMs, total_ops, hitRate);
}


/**
 * ============================================================================
 * CRITERION 4: ZIPF DISTRIBUTION (Real-World Access Pattern)
 * ============================================================================
 * 
 * @brief Тестирует реалистичный паттерн доступа (80/20 rule)
 * 
 * Сценарий:
 * - Zipf распределение: 80% трафика идёт на 20% ключей (power law)
 * - 70% операций — GET, 30% — PUT
 * - Это моделирует real-world системы
 * 
 * Real-world примеры:
 * - Веб-кэш: 80% трафика на 20% популярных сайтов
 * - CDN: 80% запросов к 20% контента
 * - СоцСети: 80% читают 20% постов
 * - Логирование: 80% пишут в 20% регионов
 * 
 * Что измеряем:
 * - Hit rate на realistic workload (должен быть выше ~80-90%)
 * - Как LRU/LFU справляется с "горячими" ключами
 * - Конкурентное преимущество хороших политик вытеснения
 * 
 * Почему это важно:
 * - Самый реалистичный тест
 * - Показывает эффективность политики вытеснения
 * - OurCache может быть быстрее если LRU лучше
 * 
 * Ожидаемый результат:
 * - Hit rate: ~85-95% (большинство "горячих" ключей в кэше)
 * - OurCache: может быть конкурентным благодаря LRU
 * - cpp-lru-cache может быть медленнее (хуже обновляет MRU)
 */
void testZipfWorkload(CacheStrategy<int, int>& strategy,
                      const BenchmarkConfig& config) {
    strategy.clear();
    
    // Заполняем кэш начальными данными
    for (size_t i = 0; i < config.cache_size; ++i) {
        strategy.put(static_cast<int>(i), static_cast<int>(i * 10));
    }
    
    // Генерируем Zipf workload (80/20 rule)
    // s=1.0 → классическое Zipf распределение
    ZipfWorkload zipf_workload(config.getKeyRange(), config.num_operations, 1.0, config.random_seed);
    auto keys = zipf_workload.generate();
    
    std::mt19937 rng(config.random_seed + 1);
    std::uniform_real_distribution<double> op_dist(0.0, 1.0);
    
    size_t hits = 0;
    size_t get_ops = 0;
    
    double timeMs = measureMs([&]() {
        for (size_t i = 0; i < config.num_operations; ++i) {
            if (op_dist(rng) < 0.7) {
                // GET operation (70%)
                int key = keys[i];
                auto result = strategy.get(key);
                if (result.has_value()) {
                    hits++;
                }
                get_ops++;
            } else {
                // PUT operation (30%)
                int key = keys[i];
                int value = static_cast<int>(key * 10);
                strategy.put(key, value);
            }
        }
    });
    
    double hitRate = get_ops > 0 ? static_cast<double>(hits) / get_ops : 0.0;
    printResult(strategy.name() + " - Zipf 80/20 (70% GET, 30% PUT)", timeMs, config.num_operations, hitRate);
}


/**
 * ============================================================================
 * CRITERION 5: TEMPORAL LOCALITY (Recent Keys Access Pattern)
 * ============================================================================
 * 
 * @brief Тестирует паттерн временной локальности
 * 
 * Сценарий:
 * - Недавно добавленные элементы более вероятны для доступа
 * - Поддерживаем "окно" из последних N элементов (recent_window)
 * - 70% операций идут к этому окну (горячие ключи)
 * - 30% операций идут ко всем ключам (холодные ключи)
 * 
 * Real-world примеры:
 * - Новостная лента: новые посты в топе (70% читают последние)
 * - Session хранилище: активные сессии недавние (70% используют "живые")
 * - Логирование: последние события чаще смотрят (70% логов последние)
 * - Очередь задач: недавние задачи выполняются чаще
 * 
 * Что измеряем:
 * - Как хорошо кэш справляется с "горячей" зоной
 * - Hit rate должен быть высокий (75-90%)
 * - Может ли простой LRU конкурировать с более сложными политиками
 * 
 * Почему это важно:
 * - Очень реалистичный для многих приложений
 * - Может показать преимущество простого LRU (оптимален для этого!)
 * - Отличается от Zipf: здесь новизна важнее популярности
 * 
 * Ожидаемый результат:
 * - Hit rate: ~80-90% (временное окно эффективнее чем Uniform)
 * - OurCache: может быть быстрее (LRU идеален для этого)
 * - Throughput выше чем Zipf (меньше вытеснений "горячих" ключей)
 * 
 * Разница между этим и Zipf:
 * - Zipf: 20% ключей популярны и ВСЕГДА в кэше
 * - Temporal: недавние ключи популярны, но меняются со временем
 * - Это важно для LRU: отлично работает с временной локальностью!
 */
void testTemporalWorkload(CacheStrategy<int, int>& strategy,
                          const BenchmarkConfig& config) {
    strategy.clear();
    
    // Заполняем кэш начальными данными
    for (size_t i = 0; i < config.cache_size; ++i) {
        strategy.put(static_cast<int>(i), static_cast<int>(i * 10));
    }
    
    // Генерируем Temporal workload
    // recent_window=1000: окно из последних 1000 элементов
    // hot_ratio=0.7: 70% обращений к этому окну
    TemporalWorkload temporal_workload(
        config.getKeyRange(),
        config.num_operations,
        1000,      // recent_window_size
        0.7,       // hot_ratio (70% к recent ключам)
        config.random_seed
    );
    auto keys = temporal_workload.generate();
    
    std::mt19937 rng(config.random_seed + 1);
    std::uniform_real_distribution<double> op_dist(0.0, 1.0);
    
    size_t hits = 0;
    size_t get_ops = 0;
    
    double timeMs = measureMs([&]() {
        for (size_t i = 0; i < config.num_operations; ++i) {
            if (op_dist(rng) < 0.7) {
                // GET operation (70%)
                int key = keys[i];
                auto result = strategy.get(key);
                if (result.has_value()) {
                    hits++;
                }
                get_ops++;
            } else {
                // PUT operation (30%)
                int key = keys[i];
                int value = static_cast<int>(key * 10);
                strategy.put(key, value);
            }
        }
    });
    
    double hitRate = get_ops > 0 ? static_cast<double>(hits) / get_ops : 0.0;
    printResult(strategy.name() + " - Temporal Locality", timeMs, config.num_operations, hitRate);
}


int main() {
    std::cout << "╔════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Cache Library Comparison Benchmark - Comprehensive Analysis   ║\n";
    std::cout << "║  Version 2.1: With Temporal Locality Test                      ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    
    BenchmarkConfig config;
    config.setStandard();
    //config.setHeavy();
    //config.setVeryHeavy();
    
    std::cout << "┌─ CONFIGURATION ──────────────────────────────────────────────┐\n";
    std::cout << "│ Cache Capacity:    " << std::setw(10) << config.cache_size << " elements\n";
    std::cout << "│ Total Operations:  " << std::setw(10) << config.num_operations << " ops\n";
    std::cout << "│ Key Range:         " << std::setw(10) << config.getKeyRange() << " keys (2x capacity)\n";
    std::cout << "│ Random Seed:       " << std::setw(10) << config.random_seed << " (reproducible)\n";
    std::cout << "└──────────────────────────────────────────────────────────────┘\n";
    
    std::cout << "\n┌─ METHODOLOGY ────────────────────────────────────────────────┐\n";
    std::cout << "│ • Metrics: Throughput (ops/sec), Latency (ms), Hit Rate (%)\n";
    std::cout << "│ • Hit Rate = (# hits) / (# GET operations)\n";
    std::cout << "│ • Throughput = (operations / time) * 1000\n";
    std::cout << "│ • Each criterion tests different access patterns\n";
    std::cout << "│ • Reproducible: same seed = same results\n";
    std::cout << "└──────────────────────────────────────────────────────────────┘\n";
    
    // ========== Test 1: Sequential PUT ==========
    printHeader(
        "Criterion 1: SEQUENTIAL PUT (Write-Heavy Baseline)",
        "└─ Measures: Pure write throughput with continuous evictions\n"
        "   Expected: cpp-lru > LRUCache11 > OurCache (due to overhead)\n"
        "   Real-world: Logging, message queues, time-series databases"
    );
    
    {
        auto strategy = std::make_unique<OurCacheStrategy<int, int>>(config.cache_size);
        testSequentialPUT(*strategy, config);
    }
    
    {
        auto strategy = std::make_unique<LRUCache11Strategy<int, int>>(config.cache_size);
        testSequentialPUT(*strategy, config);
    }
    
    {
        auto strategy = std::make_unique<CppLRUStrategy<int, int>>(config.cache_size);
        testSequentialPUT(*strategy, config);
    }
    
    // ========== Test 2: Sequential GET ==========
    printHeader(
        "Criterion 2: SEQUENTIAL GET (Read-Heavy, 100% Hit Rate)",
        "└─ Measures: Pure read throughput in ideal conditions\n"
        "   Expected: All libraries similar (same operations)\n"
        "   Real-world: Web caches, CDNs, in-memory databases"
    );
    
    {
        auto strategy = std::make_unique<OurCacheStrategy<int, int>>(config.cache_size);
        testSequentialGET(*strategy, config);
    }
    
    {
        auto strategy = std::make_unique<LRUCache11Strategy<int, int>>(config.cache_size);
        testSequentialGET(*strategy, config);
    }
    
    {
        auto strategy = std::make_unique<CppLRUStrategy<int, int>>(config.cache_size);
        testSequentialGET(*strategy, config);
    }
    
    
    // ========== Test 3: Mixed 80/20 ==========
    printHeader(
        "Criterion 3: MIXED 80/20 WORKLOAD (Balanced Scenario)",
        "└─ Measures: Balanced read/write with uniform key distribution\n"
        "   Expected: Hit rate ~50% (random access, cache << key range)\n"
        "   Real-world: Web servers, application caches, generic KV stores"
    );
    
    {
        auto strategy = std::make_unique<OurCacheStrategy<int, int>>(config.cache_size);
        testMixed8020(*strategy, config);
    }
    
    {
        auto strategy = std::make_unique<LRUCache11Strategy<int, int>>(config.cache_size);
        testMixed8020(*strategy, config);
    }
    
    {
        auto strategy = std::make_unique<CppLRUStrategy<int, int>>(config.cache_size);
        testMixed8020(*strategy, config);
    }
    
    
    // ========== Test 4: Zipf Workload ==========
    printHeader(
        "Criterion 4: ZIPF DISTRIBUTION (Real-World Pattern)",
        "└─ Measures: 80/20 rule with realistic key popularity\n"
        "   Expected: Hit rate ~85-95% (popular keys stay in cache)\n"
        "   Real-world: Web pages, social media, content delivery, logs"
    );
    
    {
        auto strategy = std::make_unique<OurCacheStrategy<int, int>>(config.cache_size);
        testZipfWorkload(*strategy, config);
    }
    
    {
        auto strategy = std::make_unique<LRUCache11Strategy<int, int>>(config.cache_size);
        testZipfWorkload(*strategy, config);
    }
    
    {
        auto strategy = std::make_unique<CppLRUStrategy<int, int>>(config.cache_size);
        testZipfWorkload(*strategy, config);
    }
    
    // ========== Test 5: Temporal Workload ==========
    printHeader(
        "Criterion 5: TEMPORAL LOCALITY (Recent Keys Pattern)",
        "└─ Measures: 70% access to recent keys, 30% to all keys\n"
        "   Expected: Hit rate ~80-90% (temporal window + recent keys)\n"
        "   Real-world: News feeds, active sessions, recent logs, task queues"
    );
    
    {
        auto strategy = std::make_unique<OurCacheStrategy<int, int>>(config.cache_size);
        testTemporalWorkload(*strategy, config);
    }
    
    {
        auto strategy = std::make_unique<LRUCache11Strategy<int, int>>(config.cache_size);
        testTemporalWorkload(*strategy, config);
    }
    
    {
        auto strategy = std::make_unique<CppLRUStrategy<int, int>>(config.cache_size);
        testTemporalWorkload(*strategy, config);
    }
    
    std::cout << "\n┌─ INTERPRETATION GUIDE ────────────────────────────────────┐\n";
    std::cout << "│ Hit Rate Analysis:\n";
    std::cout << "│   • 100%:    All requests served from cache (ideal)\n";
    std::cout << "│   • 80-99%:  Excellent cache performance\n";
    std::cout << "│   • 50-80%:  Good cache performance\n";
    std::cout << "│   • <50%:    Poor eviction policy or high key variance\n";
    std::cout << "│\n";
    std::cout << "│ Throughput Trends:\n";
    std::cout << "│   • PUT slower: Eviction overhead (Criterion 1)\n";
    std::cout << "│   • GET fastest: Simple lookup operations (Criterion 2)\n";
    std::cout << "│   • Mixed slower: Random access pattern (Criterion 3)\n";
    std::cout << "│   • Zipf faster: \"Hot\" keys stay in cache (Criterion 4)\n";
    std::cout << "│   • Temporal fastest: LRU ideal for recency! (Criterion 5)\n";
    std::cout << "│\n";
    std::cout << "│ Workload Comparison:\n";
    std::cout << "│   Criterion 3 (Uniform):   Random access, low hit rate\n";
    std::cout << "│   Criterion 4 (Zipf):      Popular keys, high hit rate\n";
    std::cout << "│   Criterion 5 (Temporal):  Recent keys, very high hit rate\n";
    std::cout << "│   → LRU excels at Temporal (built for recency!)\n";
    std::cout << "└────────────────────────────────────────────────────────────┘\n";
    
    std::cout << "\n✓ All benchmarks completed successfully!\n\n";
    
    return 0;
}
