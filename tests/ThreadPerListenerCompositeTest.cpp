#include <gtest/gtest.h>
#include <cache/listeners/ThreadPerListenerComposite.hpp>
#include <cache/listeners/StatsListener.hpp>
#include <cache/Cache.hpp>
#include <cache/eviction/LRUPolicy.hpp>

#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <mutex>

/**
 * @brief Тесты для ThreadPerListenerComposite
 * 
 * Проверяем:
 * - Добавление/удаление слушателей
 * - Доставка событий
 * - Изоляция потоков
 * - Интеграция с кэшем
 */

// ==================== Вспомогательные классы ====================

/**
 * @brief Тестовый слушатель, считающий события
 */
template<typename K, typename V>
class CountingListener : public ICacheListener<K, V> {
public:
    std::atomic<int> hitCount{0};
    std::atomic<int> missCount{0};
    std::atomic<int> insertCount{0};
    std::atomic<int> updateCount{0};
    std::atomic<int> evictCount{0};
    std::atomic<int> removeCount{0};
    std::atomic<int> clearCount{0};
    
    void onHit(const K&) override { ++hitCount; }
    void onMiss(const K&) override { ++missCount; }
    void onInsert(const K&, const V&) override { ++insertCount; }
    void onUpdate(const K&, const V&, const V&) override { ++updateCount; }
    void onEvict(const K&, const V&) override { ++evictCount; }
    void onRemove(const K&) override { ++removeCount; }
    void onClear(size_t) override { ++clearCount; }
    
    int totalEvents() const {
        return hitCount + missCount + insertCount + updateCount + 
               evictCount + removeCount + clearCount;
    }
    
    void reset() {
        hitCount = 0;
        missCount = 0;
        insertCount = 0;
        updateCount = 0;
        evictCount = 0;
        removeCount = 0;
        clearCount = 0;
    }
};

/**
 * @brief Слушатель, записывающий ID потока обработки
 */
template<typename K, typename V>
class ThreadIdListener : public ICacheListener<K, V> {
public:
    std::mutex mutex;
    std::vector<std::thread::id> threadIds;
    
    void onInsert(const K&, const V&) override {
        std::lock_guard<std::mutex> lock(mutex);
        threadIds.push_back(std::this_thread::get_id());
    }
};

/**
 * @brief Медленный слушатель для тестирования изоляции
 */
template<typename K, typename V>
class SlowListener : public ICacheListener<K, V> {
public:
    std::chrono::milliseconds delay;
    std::atomic<int> processedCount{0};
    
    explicit SlowListener(std::chrono::milliseconds d) : delay(d) {}
    
    void onInsert(const K&, const V&) override {
        std::this_thread::sleep_for(delay);
        ++processedCount;
    }
};

// ==================== Базовые операции ====================

TEST(ThreadPerListenerCompositeTest, EmptyOnCreate) {
    ThreadPerListenerComposite<std::string, int> composite;
    
    EXPECT_EQ(composite.listenerCount(), 0);
    EXPECT_EQ(composite.totalQueueSize(), 0);
}

TEST(ThreadPerListenerCompositeTest, AddListener) {
    ThreadPerListenerComposite<std::string, int> composite;
    auto listener = std::make_shared<CountingListener<std::string, int>>();
    
    composite.addListener(listener);
    
    EXPECT_EQ(composite.listenerCount(), 1);
}

TEST(ThreadPerListenerCompositeTest, AddMultipleListeners) {
    ThreadPerListenerComposite<std::string, int> composite;
    
    composite.addListener(std::make_shared<CountingListener<std::string, int>>());
    composite.addListener(std::make_shared<CountingListener<std::string, int>>());
    composite.addListener(std::make_shared<CountingListener<std::string, int>>());
    
    EXPECT_EQ(composite.listenerCount(), 3);
}

TEST(ThreadPerListenerCompositeTest, AddNullListenerIgnored) {
    ThreadPerListenerComposite<std::string, int> composite;
    
    composite.addListener(nullptr);
    
    EXPECT_EQ(composite.listenerCount(), 0);
}

TEST(ThreadPerListenerCompositeTest, RemoveListener) {
    ThreadPerListenerComposite<std::string, int> composite;
    auto listener = std::make_shared<CountingListener<std::string, int>>();
    
    composite.addListener(listener);
    EXPECT_EQ(composite.listenerCount(), 1);
    
    bool removed = composite.removeListener(listener);
    
    EXPECT_TRUE(removed);
    EXPECT_EQ(composite.listenerCount(), 0);
}

TEST(ThreadPerListenerCompositeTest, RemoveNonExistentListener) {
    ThreadPerListenerComposite<std::string, int> composite;
    auto listener1 = std::make_shared<CountingListener<std::string, int>>();
    auto listener2 = std::make_shared<CountingListener<std::string, int>>();
    
    composite.addListener(listener1);
    
    bool removed = composite.removeListener(listener2);
    
    EXPECT_FALSE(removed);
    EXPECT_EQ(composite.listenerCount(), 1);
}

TEST(ThreadPerListenerCompositeTest, Stop) {
    ThreadPerListenerComposite<std::string, int> composite;
    composite.addListener(std::make_shared<CountingListener<std::string, int>>());
    composite.addListener(std::make_shared<CountingListener<std::string, int>>());
    
    composite.stop();
    
    EXPECT_EQ(composite.listenerCount(), 0);
}

// ==================== Доставка событий ====================

TEST(ThreadPerListenerCompositeTest, EventDelivery) {
    ThreadPerListenerComposite<std::string, int> composite;
    auto listener = std::make_shared<CountingListener<std::string, int>>();
    composite.addListener(listener);
    
    composite.onInsert("key", 42);
    
    // Ждём обработки
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_EQ(listener->insertCount, 1);
}

TEST(ThreadPerListenerCompositeTest, AllEventTypesDelivered) {
    ThreadPerListenerComposite<std::string, int> composite;
    auto listener = std::make_shared<CountingListener<std::string, int>>();
    composite.addListener(listener);
    
    composite.onHit("key");
    composite.onMiss("key");
    composite.onInsert("key", 1);
    composite.onUpdate("key", 1, 2);
    composite.onEvict("key", 1);
    composite.onRemove("key");
    composite.onClear(5);
    
    // Ждём обработки
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_EQ(listener->hitCount, 1);
    EXPECT_EQ(listener->missCount, 1);
    EXPECT_EQ(listener->insertCount, 1);
    EXPECT_EQ(listener->updateCount, 1);
    EXPECT_EQ(listener->evictCount, 1);
    EXPECT_EQ(listener->removeCount, 1);
    EXPECT_EQ(listener->clearCount, 1);
}

TEST(ThreadPerListenerCompositeTest, BroadcastToAllListeners) {
    ThreadPerListenerComposite<std::string, int> composite;
    auto listener1 = std::make_shared<CountingListener<std::string, int>>();
    auto listener2 = std::make_shared<CountingListener<std::string, int>>();
    auto listener3 = std::make_shared<CountingListener<std::string, int>>();
    
    composite.addListener(listener1);
    composite.addListener(listener2);
    composite.addListener(listener3);
    
    composite.onInsert("key", 42);
    
    // Ждём обработки
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_EQ(listener1->insertCount, 1);
    EXPECT_EQ(listener2->insertCount, 1);
    EXPECT_EQ(listener3->insertCount, 1);
}

TEST(ThreadPerListenerCompositeTest, ManyEventsDelivered) {
    ThreadPerListenerComposite<std::string, int> composite;
    auto listener = std::make_shared<CountingListener<std::string, int>>();
    composite.addListener(listener);
    
    const int eventCount = 100;
    for (int i = 0; i < eventCount; ++i) {
        composite.onInsert("key" + std::to_string(i), i);
    }
    
    // Ждём обработки всех событий
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_EQ(listener->insertCount, eventCount);
}

// ==================== Изоляция потоков ====================

TEST(ThreadPerListenerCompositeTest, EachListenerHasOwnThread) {
    ThreadPerListenerComposite<std::string, int> composite;
    auto listener1 = std::make_shared<ThreadIdListener<std::string, int>>();
    auto listener2 = std::make_shared<ThreadIdListener<std::string, int>>();
    
    composite.addListener(listener1);
    composite.addListener(listener2);
    
    // Несколько событий
    for (int i = 0; i < 5; ++i) {
        composite.onInsert("key", i);
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Проверяем что у каждого слушателя свой поток
    ASSERT_FALSE(listener1->threadIds.empty());
    ASSERT_FALSE(listener2->threadIds.empty());
    
    // Все события одного слушателя обработаны одним потоком
    std::thread::id thread1 = listener1->threadIds[0];
    std::thread::id thread2 = listener2->threadIds[0];
    
    for (auto id : listener1->threadIds) {
        EXPECT_EQ(id, thread1);
    }
    for (auto id : listener2->threadIds) {
        EXPECT_EQ(id, thread2);
    }
    
    // Разные слушатели — разные потоки
    EXPECT_NE(thread1, thread2);
}

TEST(ThreadPerListenerCompositeTest, SlowListenerDoesNotBlockFast) {
    ThreadPerListenerComposite<std::string, int> composite;
    
    // Медленный слушатель — 50ms на событие
    auto slowListener = std::make_shared<SlowListener<std::string, int>>(
        std::chrono::milliseconds(50)
    );
    
    // Быстрый слушатель
    auto fastListener = std::make_shared<CountingListener<std::string, int>>();
    
    composite.addListener(slowListener);
    composite.addListener(fastListener);
    
    // 10 событий
    for (int i = 0; i < 10; ++i) {
        composite.onInsert("key", i);
    }
    
    // Быстрый должен обработать почти мгновенно
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(fastListener->insertCount, 10);
    
    // Медленный — только часть
    EXPECT_LT(slowListener->processedCount, 5);
    
    // Ждём пока медленный догонит
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    EXPECT_EQ(slowListener->processedCount, 10);
}

TEST(ThreadPerListenerCompositeTest, MainThreadNotBlocked) {
    ThreadPerListenerComposite<std::string, int> composite;
    
    // Очень медленный слушатель
    auto slowListener = std::make_shared<SlowListener<std::string, int>>(
        std::chrono::milliseconds(100)
    );
    composite.addListener(slowListener);
    
    auto start = std::chrono::steady_clock::now();
    
    // Много событий
    for (int i = 0; i < 10; ++i) {
        composite.onInsert("key", i);
    }
    
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    
    // Основной поток не должен блокироваться
    EXPECT_LT(elapsedMs, 50);  // Все push() заняли < 50ms
    
    // А медленный слушатель ещё работает
    EXPECT_LT(slowListener->processedCount, 10);
}

// ==================== Корректное завершение ====================

TEST(ThreadPerListenerCompositeTest, StopDrainsQueue) {
    ThreadPerListenerComposite<std::string, int> composite;
    auto listener = std::make_shared<CountingListener<std::string, int>>();
    composite.addListener(listener);
    
    // Много событий
    for (int i = 0; i < 100; ++i) {
        composite.onInsert("key", i);
    }
    
    // Stop должен дождаться обработки всех
    composite.stop();
    
    EXPECT_EQ(listener->insertCount, 100);
}

TEST(ThreadPerListenerCompositeTest, DestructorStopsThreads) {
    auto listener = std::make_shared<CountingListener<std::string, int>>();
    
    {
        ThreadPerListenerComposite<std::string, int> composite;
        composite.addListener(listener);
        
        for (int i = 0; i < 50; ++i) {
            composite.onInsert("key", i);
        }
        
        // Деструктор должен корректно остановить потоки
    }
    
    // После деструктора все события должны быть обработаны
    EXPECT_EQ(listener->insertCount, 50);
}

// ==================== Интеграция с кэшем ====================

TEST(ThreadPerListenerCompositeTest, IntegrationWithCache) {
    // Создаём кэш
    auto cache = Cache<std::string, int>(
        10,
        std::make_unique<LRUPolicy<std::string>>()
    );
    
    // Создаём композит
    auto composite = std::make_shared<ThreadPerListenerComposite<std::string, int>>();
    auto stats = std::make_shared<CountingListener<std::string, int>>();
    composite->addListener(stats);
    
    // Регистрируем в кэше
    cache.addListener(composite);
    
    // Операции с кэшем
    cache.put("a", 1);
    cache.put("b", 2);
    cache.get("a");      // hit
    cache.get("c");      // miss
    cache.remove("b");
    
    // Ждём обработки
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_EQ(stats->insertCount, 2);    // 2 put
    EXPECT_EQ(stats->hitCount, 1);    // 1 hit (get "a")
    EXPECT_EQ(stats->missCount, 1);   // 1 miss (get "c")
    EXPECT_EQ(stats->removeCount, 1); // 1 remove
}

TEST(ThreadPerListenerCompositeTest, IntegrationWithEviction) {
    // Маленький кэш для вытеснений
    auto cache = Cache<std::string, int>(
        2,
        std::make_unique<LRUPolicy<std::string>>()
    );
    
    auto composite = std::make_shared<ThreadPerListenerComposite<std::string, int>>();
    auto stats = std::make_shared<CountingListener<std::string, int>>();
    composite->addListener(stats);
    cache.addListener(composite);
    
    cache.put("a", 1);
    cache.put("b", 2);
    cache.put("c", 3);  // Вытеснит "a"
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_EQ(stats->insertCount, 3);    // 3 put
    EXPECT_EQ(stats->evictCount, 1);  // 1 evict ("a")
}

TEST(ThreadPerListenerCompositeTest, WithStatsListener) {
    auto cache = Cache<std::string, int>(
        100,
        std::make_unique<LRUPolicy<std::string>>()
    );
    
    auto composite = std::make_shared<ThreadPerListenerComposite<std::string, int>>();
    auto stats = std::make_shared<StatsListener<std::string, int>>();
    composite->addListener(stats);
    cache.addListener(composite);
    
    // Операции
    cache.put("key", 42);
    cache.get("key");      // hit
    cache.get("key");      // hit
    cache.get("missing");  // miss
    
    // Ждём асинхронной обработки
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_EQ(stats->hits(), 2);
    EXPECT_EQ(stats->misses(), 1);
    EXPECT_EQ(stats->inserts(), 1);
    EXPECT_DOUBLE_EQ(stats->hitRate(), 2.0 / 3.0);
}

// ==================== Stress тесты ====================

TEST(ThreadPerListenerCompositeTest, HighThroughput) {
    ThreadPerListenerComposite<int, int> composite;
    auto listener = std::make_shared<CountingListener<int, int>>();
    composite.addListener(listener);
    
    const int eventCount = 10000;
    
    auto start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < eventCount; ++i) {
        composite.onInsert(i, i);
    }
    
    auto publishTime = std::chrono::steady_clock::now() - start;
    auto publishMs = std::chrono::duration_cast<std::chrono::milliseconds>(publishTime).count();
    
    // Публикация должна быть очень быстрой
    EXPECT_LT(publishMs, 100);  // 10000 событий < 100ms
    
    // Ждём обработки
    composite.stop();
    
    EXPECT_EQ(listener->insertCount, eventCount);
}

TEST(ThreadPerListenerCompositeTest, ConcurrentAddRemoveListeners) {
    ThreadPerListenerComposite<std::string, int> composite;
    std::atomic<bool> running{true};
    
    // Поток, добавляющий/удаляющий слушателей
    std::thread modifier([&]() {
        for (int i = 0; i < 100 && running; ++i) {
            auto listener = std::make_shared<CountingListener<std::string, int>>();
            composite.addListener(listener);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            composite.removeListener(listener);
        }
    });
    
    // Поток, генерирующий события
    std::thread producer([&]() {
        for (int i = 0; i < 1000; ++i) {
            composite.onInsert("key", i);
        }
        running = false;
    });
    
    producer.join();
    modifier.join();
    composite.stop();
    
    // Главное — не упасть
    SUCCEED();
}