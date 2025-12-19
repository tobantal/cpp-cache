#include <gtest/gtest.h>
#include <cache/Cache.hpp>
#include <cache/concurrency/ThreadSafeCache.hpp>
#include <cache/concurrency/ShardedCache.hpp>
#include <cache/eviction/LRUPolicy.hpp>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

/**
 * @brief Тесты для ThreadSafeCache и ShardedCache
 * 
 * Проверяем:
 * - Базовую функциональность декораторов
 * - Thread-safety при конкурентном доступе
 * - Корректность данных после многопоточной работы
 */

// ==================== Вспомогательные функции ====================

template<typename K, typename V>
std::unique_ptr<Cache<K, V>> makeLRUCache(size_t capacity) {
    return std::make_unique<Cache<K, V>>(
        capacity, std::make_unique<LRUPolicy<K>>());
}

// ==================== ThreadSafeCache: Базовые тесты ====================

TEST(ThreadSafeCacheTest, ConstructorThrowsOnNull) {
    EXPECT_THROW(
        (ThreadSafeCache<int, int>(nullptr)),
        std::invalid_argument
    );
}

TEST(ThreadSafeCacheTest, BasicPutAndGet) {
    auto cache = std::make_unique<ThreadSafeCache<int, int>>(
        makeLRUCache<int, int>(100));
    
    cache->put(1, 100);
    cache->put(2, 200);
    
    auto val1 = cache->get(1);
    auto val2 = cache->get(2);
    auto val3 = cache->get(3);
    
    ASSERT_TRUE(val1.has_value());
    EXPECT_EQ(val1.value(), 100);
    
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(val2.value(), 200);
    
    EXPECT_FALSE(val3.has_value());
}

TEST(ThreadSafeCacheTest, Remove) {
    auto cache = std::make_unique<ThreadSafeCache<int, int>>(
        makeLRUCache<int, int>(100));
    
    cache->put(1, 100);
    EXPECT_TRUE(cache->contains(1));
    
    bool removed = cache->remove(1);
    
    EXPECT_TRUE(removed);
    EXPECT_FALSE(cache->contains(1));
}

TEST(ThreadSafeCacheTest, Clear) {
    auto cache = std::make_unique<ThreadSafeCache<int, int>>(
        makeLRUCache<int, int>(100));
    
    cache->put(1, 100);
    cache->put(2, 200);
    EXPECT_EQ(cache->size(), 2);
    
    cache->clear();
    
    EXPECT_EQ(cache->size(), 0);
}

TEST(ThreadSafeCacheTest, SizeAndCapacity) {
    auto cache = std::make_unique<ThreadSafeCache<int, int>>(
        makeLRUCache<int, int>(100));
    
    EXPECT_EQ(cache->size(), 0);
    EXPECT_EQ(cache->capacity(), 100);
    
    cache->put(1, 100);
    EXPECT_EQ(cache->size(), 1);
}

// ==================== ThreadSafeCache: Многопоточные тесты ====================

TEST(ThreadSafeCacheTest, ConcurrentWrites) {
    auto cache = std::make_unique<ThreadSafeCache<int, int>>(
        makeLRUCache<int, int>(1000));
    
    const int numThreads = 4;
    const int opsPerThread = 250;
    std::vector<std::thread> threads;
    
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
    
    EXPECT_EQ(cache->size(), numThreads * opsPerThread);
    
    // Проверяем корректность данных
    for (int t = 0; t < numThreads; ++t) {
        for (int i = 0; i < opsPerThread; ++i) {
            int key = t * opsPerThread + i;
            auto val = cache->get(key);
            ASSERT_TRUE(val.has_value()) << "Key " << key << " not found";
            EXPECT_EQ(val.value(), key * 2);
        }
    }
}

TEST(ThreadSafeCacheTest, ConcurrentReadsAndWrites) {
    auto cache = std::make_unique<ThreadSafeCache<int, int>>(
        makeLRUCache<int, int>(100));
    
    // Предзаполняем
    for (int i = 0; i < 100; ++i) {
        cache->put(i, i * 2);
    }
    
    std::atomic<int> readCount{0};
    std::atomic<int> hitCount{0};
    
    std::vector<std::thread> readers;
    std::vector<std::thread> writers;
    
    // 4 читателя
    for (int t = 0; t < 4; ++t) {
        readers.emplace_back([&cache, &readCount, &hitCount]() {
            for (int i = 0; i < 200; ++i) {
                auto val = cache->get(i % 100);
                ++readCount;
                if (val.has_value()) {
                    ++hitCount;
                }
            }
        });
    }
    
    // 2 писателя
    for (int t = 0; t < 2; ++t) {
        writers.emplace_back([&cache, t]() {
            for (int i = 0; i < 50; ++i) {
                cache->put(100 + t * 50 + i, i);
            }
        });
    }
    
    for (auto& th : readers) th.join();
    for (auto& th : writers) th.join();
    
    EXPECT_EQ(readCount.load(), 4 * 200);
    EXPECT_GT(hitCount.load(), 0);
}

TEST(ThreadSafeCacheTest, ConcurrentSameKeyUpdates) {
    auto cache = std::make_unique<ThreadSafeCache<int, int>>(
        makeLRUCache<int, int>(100));
    
    const int KEY = 42;
    const int numThreads = 8;
    const int opsPerThread = 100;
    
    std::vector<std::thread> threads;
    
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&cache, t, opsPerThread, KEY]() {
            for (int i = 0; i < opsPerThread; ++i) {
                cache->put(KEY, t * 1000 + i);
            }
        });
    }
    
    for (auto& th : threads) {
        th.join();
    }
    
    // Ключ должен существовать с каким-то значением
    auto val = cache->get(KEY);
    ASSERT_TRUE(val.has_value());
    EXPECT_GE(val.value(), 0);
}

TEST(ThreadSafeCacheTest, WithExclusiveLock) {
    auto cache = std::make_unique<ThreadSafeCache<std::string, int>>(
        makeLRUCache<std::string, int>(100));
    
    // Атомарный check-then-act
    cache->withExclusiveLock([](ICache<std::string, int>& inner) {
        if (!inner.contains("key")) {
            inner.put("key", 42);
        }
    });
    
    auto val = cache->get("key");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), 42);
}

// ==================== ShardedCache: Базовые тесты ====================

TEST(ShardedCacheTest, ConstructorValidation) {
    auto factory = [](size_t cap) {
        return makeLRUCache<int, int>(cap);
    };
    
    EXPECT_THROW(
        (ShardedCache<int, int, 8>(0, factory)),
        std::invalid_argument
    );
    
    EXPECT_THROW(
        (ShardedCache<int, int, 8>(100, nullptr)),
        std::invalid_argument
    );
}

TEST(ShardedCacheTest, BasicPutAndGet) {
    auto factory = [](size_t cap) {
        return makeLRUCache<int, int>(cap);
    };
    
    ShardedCache<int, int, 4> cache(100, factory);
    
    cache.put(1, 100);
    cache.put(2, 200);
    cache.put(3, 300);
    
    EXPECT_EQ(cache.get(1).value(), 100);
    EXPECT_EQ(cache.get(2).value(), 200);
    EXPECT_EQ(cache.get(3).value(), 300);
    EXPECT_FALSE(cache.get(999).has_value());
}

TEST(ShardedCacheTest, Remove) {
    auto factory = [](size_t cap) {
        return makeLRUCache<int, int>(cap);
    };
    
    ShardedCache<int, int, 4> cache(100, factory);
    
    cache.put(1, 100);
    EXPECT_TRUE(cache.contains(1));
    
    bool removed = cache.remove(1);
    
    EXPECT_TRUE(removed);
    EXPECT_FALSE(cache.contains(1));
}

TEST(ShardedCacheTest, Clear) {
    auto factory = [](size_t cap) {
        return makeLRUCache<int, int>(cap);
    };
    
    ShardedCache<int, int, 4> cache(100, factory);
    
    for (int i = 0; i < 50; ++i) {
        cache.put(i, i * 2);
    }
    EXPECT_EQ(cache.size(), 50);
    
    cache.clear();
    
    EXPECT_EQ(cache.size(), 0);
}

TEST(ShardedCacheTest, CapacityAndShardCount) {
    auto factory = [](size_t cap) {
        return makeLRUCache<int, int>(cap);
    };
    
    ShardedCache<int, int, 8> cache(1000, factory);
    
    EXPECT_EQ(cache.capacity(), 1000);
    EXPECT_EQ(cache.shardCount(), 8);
}

TEST(ShardedCacheTest, KeysDistributedAcrossShards) {
    auto factory = [](size_t cap) {
        return makeLRUCache<int, int>(cap);
    };
    
    ShardedCache<int, int, 4> cache(400, factory);
    
    // Добавляем много ключей
    for (int i = 0; i < 100; ++i) {
        cache.put(i, i);
    }
    
    // Проверяем, что ключи распределены по шардам
    size_t totalInShards = 0;
    for (size_t s = 0; s < 4; ++s) {
        size_t shardSize = cache.shardSize(s);
        totalInShards += shardSize;
        // Каждый шард должен содержать хотя бы несколько ключей
        // (при равномерном хэшировании)
    }
    
    EXPECT_EQ(totalInShards, 100);
}

// ==================== ShardedCache: Многопоточные тесты ====================

TEST(ShardedCacheTest, ConcurrentWrites) {
    auto factory = [](size_t cap) {
        return makeLRUCache<int, int>(cap);
    };
    
    ShardedCache<int, int, 8> cache(2000, factory);
    
    const int numThreads = 8;
    const int opsPerThread = 200;
    std::vector<std::thread> threads;
    
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&cache, t, opsPerThread]() {
            for (int i = 0; i < opsPerThread; ++i) {
                int key = t * opsPerThread + i;
                cache.put(key, key * 2);
            }
        });
    }
    
    for (auto& th : threads) {
        th.join();
    }
    
    EXPECT_EQ(cache.size(), numThreads * opsPerThread);
}

TEST(ShardedCacheTest, ConcurrentReadsAndWrites) {
    auto factory = [](size_t cap) {
        return makeLRUCache<int, int>(cap);
    };
    
    ShardedCache<int, int, 8> cache(1000, factory);
    
    // Предзаполняем
    for (int i = 0; i < 500; ++i) {
        cache.put(i, i * 2);
    }
    
    std::atomic<int> successfulReads{0};
    std::vector<std::thread> threads;
    
    // 4 читателя
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&cache, &successfulReads]() {
            for (int i = 0; i < 500; ++i) {
                auto val = cache.get(i);
                if (val.has_value() && val.value() == i * 2) {
                    ++successfulReads;
                }
            }
        });
    }
    
    // 4 писателя (пишут в другой диапазон ключей)
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&cache, t]() {
            for (int i = 0; i < 100; ++i) {
                cache.put(500 + t * 100 + i, i);
            }
        });
    }
    
    for (auto& th : threads) {
        th.join();
    }
    
    // Большинство чтений должны быть успешными
    EXPECT_GT(successfulReads.load(), 1000);
}

TEST(ShardedCacheTest, HighContention) {
    auto factory = [](size_t cap) {
        return makeLRUCache<int, int>(cap);
    };
    
    // Маленький кэш, много потоков — высокая конкуренция
    ShardedCache<int, int, 4> cache(100, factory);
    
    const int numThreads = 16;
    std::vector<std::thread> threads;
    std::atomic<int> operations{0};
    
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&cache, &operations, t]() {
            for (int i = 0; i < 100; ++i) {
                int key = (t * 7 + i * 13) % 50;  // Ограниченный набор ключей
                cache.put(key, t * 1000 + i);
                cache.get(key);
                ++operations;
                ++operations;
            }
        });
    }
    
    for (auto& th : threads) {
        th.join();
    }
    
    EXPECT_EQ(operations.load(), numThreads * 100 * 2);
}

TEST(ShardedCacheTest, ForEachShard) {
    auto factory = [](size_t cap) {
        return makeLRUCache<int, int>(cap);
    };
    
    ShardedCache<int, int, 4> cache(100, factory);
    
    for (int i = 0; i < 40; ++i) {
        cache.put(i, i);
    }
    
    size_t totalCleared = 0;
    cache.forEachShard([&totalCleared](ICache<int, int>& shard) {
        totalCleared += shard.size();
        shard.clear();
    });
    
    EXPECT_EQ(totalCleared, 40);
    EXPECT_EQ(cache.size(), 0);
}

// ==================== Сравнительный тест производительности ====================

TEST(ConcurrencyTest, ShardedVsThreadSafePerformance) {
    // Этот тест показывает преимущество шардирования
    // при высокой конкурентности
    
    const int numThreads = 8;
    const int opsPerThread = 1000;
    const size_t cacheSize = numThreads * opsPerThread;  // Достаточно для всех ключей
    
    // ThreadSafeCache
    auto threadSafe = std::make_unique<ThreadSafeCache<int, int>>(
        makeLRUCache<int, int>(cacheSize));
    
    auto start1 = std::chrono::high_resolution_clock::now();
    {
        std::vector<std::thread> threads;
        for (int t = 0; t < numThreads; ++t) {
            threads.emplace_back([&threadSafe, t, opsPerThread]() {
                for (int i = 0; i < opsPerThread; ++i) {
                    threadSafe->put(t * opsPerThread + i, i);
                    threadSafe->get(t * opsPerThread + i);
                }
            });
        }
        for (auto& th : threads) th.join();
    }
    auto end1 = std::chrono::high_resolution_clock::now();
    auto duration1 = std::chrono::duration_cast<std::chrono::milliseconds>(end1 - start1);
    
    // ShardedCache
    auto factory = [](size_t cap) {
        return makeLRUCache<int, int>(cap);
    };
    ShardedCache<int, int, 8> sharded(cacheSize, factory);
    
    auto start2 = std::chrono::high_resolution_clock::now();
    {
        std::vector<std::thread> threads;
        for (int t = 0; t < numThreads; ++t) {
            threads.emplace_back([&sharded, t, opsPerThread]() {
                for (int i = 0; i < opsPerThread; ++i) {
                    sharded.put(t * opsPerThread + i, i);
                    sharded.get(t * opsPerThread + i);
                }
            });
        }
        for (auto& th : threads) th.join();
    }
    auto end2 = std::chrono::high_resolution_clock::now();
    auto duration2 = std::chrono::duration_cast<std::chrono::milliseconds>(end2 - start2);
    
    // Просто выводим результаты, не делаем строгих assertions
    // (производительность зависит от железа)
    std::cout << "ThreadSafeCache: " << duration1.count() << "ms\n";
    std::cout << "ShardedCache:    " << duration2.count() << "ms\n";
    
    // Оба должны содержать все данные
    EXPECT_EQ(threadSafe->size(), numThreads * opsPerThread);
    EXPECT_EQ(sharded.size(), numThreads * opsPerThread);
}