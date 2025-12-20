#include <gtest/gtest.h>
#include <cache/utils/ThreadSafeQueue.hpp>

#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <set>

/**
 * @brief Тесты для ThreadSafeQueue
 * 
 * Проверяем:
 * - Базовые операции (push, pop, tryPop)
 * - Таймауты
 * - Shutdown
 * - Многопоточность
 */

// ==================== Базовые операции ====================

TEST(ThreadSafeQueueTest, EmptyOnCreate) {
    ThreadSafeQueue<int> queue;
    
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0);
}

TEST(ThreadSafeQueueTest, PushAndSize) {
    ThreadSafeQueue<int> queue;
    
    queue.push(1);
    queue.push(2);
    queue.push(3);
    
    EXPECT_FALSE(queue.empty());
    EXPECT_EQ(queue.size(), 3);
}

TEST(ThreadSafeQueueTest, TryPopImmediate) {
    ThreadSafeQueue<int> queue;
    queue.push(42);
    
    int value = 0;
    bool success = queue.tryPopImmediate(value);
    
    EXPECT_TRUE(success);
    EXPECT_EQ(value, 42);
    EXPECT_TRUE(queue.empty());
}

TEST(ThreadSafeQueueTest, TryPopImmediateEmpty) {
    ThreadSafeQueue<int> queue;
    
    int value = 0;
    bool success = queue.tryPopImmediate(value);
    
    EXPECT_FALSE(success);
}

TEST(ThreadSafeQueueTest, TryPopWithTimeout) {
    ThreadSafeQueue<int> queue;
    queue.push(42);
    
    int value = 0;
    bool success = queue.tryPop(value, std::chrono::milliseconds(100));
    
    EXPECT_TRUE(success);
    EXPECT_EQ(value, 42);
}

TEST(ThreadSafeQueueTest, TryPopTimeoutExpires) {
    ThreadSafeQueue<int> queue;
    
    auto start = std::chrono::steady_clock::now();
    
    int value = 0;
    bool success = queue.tryPop(value, std::chrono::milliseconds(50));
    
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    
    EXPECT_FALSE(success);
    EXPECT_GE(elapsedMs, 45);  // Небольшой допуск
    EXPECT_LT(elapsedMs, 200); // Но не слишком долго
}

TEST(ThreadSafeQueueTest, TryPopOptional) {
    ThreadSafeQueue<int> queue;
    queue.push(42);
    
    auto result = queue.tryPop(std::chrono::milliseconds(100));
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 42);
}

TEST(ThreadSafeQueueTest, TryPopOptionalEmpty) {
    ThreadSafeQueue<int> queue;
    
    auto result = queue.tryPop(std::chrono::milliseconds(10));
    
    EXPECT_FALSE(result.has_value());
}

TEST(ThreadSafeQueueTest, FifoOrder) {
    ThreadSafeQueue<int> queue;
    
    queue.push(1);
    queue.push(2);
    queue.push(3);
    
    int v1, v2, v3;
    queue.tryPopImmediate(v1);
    queue.tryPopImmediate(v2);
    queue.tryPopImmediate(v3);
    
    EXPECT_EQ(v1, 1);
    EXPECT_EQ(v2, 2);
    EXPECT_EQ(v3, 3);
}

TEST(ThreadSafeQueueTest, PushBatch) {
    ThreadSafeQueue<int> queue;
    
    queue.pushBatch({1, 2, 3, 4, 5});
    
    EXPECT_EQ(queue.size(), 5);
    
    int value;
    queue.tryPopImmediate(value);
    EXPECT_EQ(value, 1);
}

TEST(ThreadSafeQueueTest, Clear) {
    ThreadSafeQueue<int> queue;
    queue.push(1);
    queue.push(2);
    queue.push(3);
    
    queue.clear();
    
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0);
}

// ==================== Shutdown ====================

TEST(ThreadSafeQueueTest, ShutdownUnblocksWaitingThread) {
    ThreadSafeQueue<int> queue;
    std::atomic<bool> popReturned{false};
    std::atomic<bool> popSuccess{true};
    
    // Поток ждёт в pop()
    std::thread consumer([&]() {
        int value;
        popSuccess = queue.pop(value);
        popReturned = true;
    });
    
    // Даём время начать ожидание
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(popReturned);  // Ещё ждёт
    
    // Shutdown разблокирует
    queue.shutdown();
    consumer.join();
    
    EXPECT_TRUE(popReturned);
    EXPECT_FALSE(popSuccess);   // Вернул false (нет данных)
}

TEST(ThreadSafeQueueTest, ShutdownAllowsDraining) {
    ThreadSafeQueue<int> queue;
    queue.push(1);
    queue.push(2);
    
    queue.shutdown();
    
    // После shutdown можно извлечь оставшиеся элементы
    int v1, v2;
    EXPECT_TRUE(queue.tryPopImmediate(v1));
    EXPECT_TRUE(queue.tryPopImmediate(v2));
    EXPECT_EQ(v1, 1);
    EXPECT_EQ(v2, 2);
    
    // После опустошения — false
    int v3;
    EXPECT_FALSE(queue.tryPopImmediate(v3));
}

TEST(ThreadSafeQueueTest, IsShutdown) {
    ThreadSafeQueue<int> queue;
    
    EXPECT_FALSE(queue.isShutdown());
    
    queue.shutdown();
    
    EXPECT_TRUE(queue.isShutdown());
}

TEST(ThreadSafeQueueTest, PushAfterShutdownWorks) {
    ThreadSafeQueue<int> queue;
    queue.shutdown();
    
    // Push работает даже после shutdown
    queue.push(42);
    
    int value;
    EXPECT_TRUE(queue.tryPopImmediate(value));
    EXPECT_EQ(value, 42);
}

// ==================== Многопоточность ====================

TEST(ThreadSafeQueueTest, SingleProducerSingleConsumer) {
    ThreadSafeQueue<int> queue;
    const int count = 1000;
    std::vector<int> received;
    received.reserve(count);
    
    // Consumer
    std::thread consumer([&]() {
        for (int i = 0; i < count; ++i) {
            int value;
            if (queue.pop(value)) {
                received.push_back(value);
            }
        }
    });
    
    // Producer
    for (int i = 0; i < count; ++i) {
        queue.push(i);
    }
    
    consumer.join();
    
    // Проверяем что все получены в правильном порядке
    ASSERT_EQ(received.size(), count);
    for (int i = 0; i < count; ++i) {
        EXPECT_EQ(received[i], i);
    }
}

TEST(ThreadSafeQueueTest, MultipleProducersSingleConsumer) {
    ThreadSafeQueue<int> queue;
    const int producerCount = 4;
    const int itemsPerProducer = 250;
    const int totalItems = producerCount * itemsPerProducer;
    
    std::atomic<int> receivedCount{0};
    std::atomic<bool> consumerDone{false};
    
    // Consumer
    std::thread consumer([&]() {
        while (receivedCount < totalItems) {
            int value;
            if (queue.tryPop(value, std::chrono::milliseconds(10))) {
                ++receivedCount;
            }
        }
        consumerDone = true;
    });
    
    // Producers
    std::vector<std::thread> producers;
    for (int p = 0; p < producerCount; ++p) {
        producers.emplace_back([&, p]() {
            for (int i = 0; i < itemsPerProducer; ++i) {
                queue.push(p * 1000 + i);
            }
        });
    }
    
    for (auto& t : producers) {
        t.join();
    }
    consumer.join();
    
    EXPECT_EQ(receivedCount, totalItems);
}

TEST(ThreadSafeQueueTest, MultipleConsumers) {
    ThreadSafeQueue<int> queue;
    const int itemCount = 1000;
    const int consumerCount = 4;
    
    std::atomic<int> totalReceived{0};
    std::set<int> allReceived;
    std::mutex receivedMutex;
    
    // Consumers
    std::vector<std::thread> consumers;
    for (int c = 0; c < consumerCount; ++c) {
        consumers.emplace_back([&]() {
            while (true) {
                int value;
                if (queue.tryPop(value, std::chrono::milliseconds(50))) {
                    ++totalReceived;
                    std::lock_guard<std::mutex> lock(receivedMutex);
                    allReceived.insert(value);
                } else if (queue.isShutdown() && queue.empty()) {
                    break;
                }
            }
        });
    }
    
    // Producer
    for (int i = 0; i < itemCount; ++i) {
        queue.push(i);
    }
    
    // Даём время обработать
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    queue.shutdown();
    
    for (auto& t : consumers) {
        t.join();
    }
    
    // Каждый элемент получен ровно один раз
    EXPECT_EQ(totalReceived, itemCount);
    EXPECT_EQ(allReceived.size(), itemCount);
}

TEST(ThreadSafeQueueTest, StressTest) {
    ThreadSafeQueue<int> queue;
    const int iterations = 10000;
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    
    auto producer = [&]() {
        for (int i = 0; i < iterations; ++i) {
            queue.push(i);
            ++produced;
        }
    };
    
    auto consumer = [&]() {
        while (consumed < iterations * 2) {  // 2 producers
            int value;
            if (queue.tryPop(value, std::chrono::milliseconds(1))) {
                ++consumed;
            }
        }
    };
    
    std::thread p1(producer);
    std::thread p2(producer);
    std::thread c1(consumer);
    std::thread c2(consumer);
    
    p1.join();
    p2.join();
    
    // Ждём пока consumers закончат
    while (consumed < produced) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    queue.shutdown();
    c1.join();
    c2.join();
    
    EXPECT_EQ(consumed, produced);
}

// ==================== Граничные случаи ====================

TEST(ThreadSafeQueueTest, MoveOnlyType) {
    ThreadSafeQueue<std::unique_ptr<int>> queue;
    
    queue.push(std::make_unique<int>(42));
    
    std::unique_ptr<int> value;
    EXPECT_TRUE(queue.tryPopImmediate(value));
    EXPECT_EQ(*value, 42);
}

TEST(ThreadSafeQueueTest, LargeObjects) {
    struct LargeObject {
        std::array<char, 1024> data;
        int id;
    };
    
    ThreadSafeQueue<LargeObject> queue;
    
    LargeObject obj;
    obj.id = 123;
    queue.push(std::move(obj));
    
    LargeObject received;
    EXPECT_TRUE(queue.tryPopImmediate(received));
    EXPECT_EQ(received.id, 123);
}

TEST(ThreadSafeQueueTest, StringQueue) {
    ThreadSafeQueue<std::string> queue;
    
    queue.push("hello");
    queue.push("world");
    
    std::string s1, s2;
    queue.tryPopImmediate(s1);
    queue.tryPopImmediate(s2);
    
    EXPECT_EQ(s1, "hello");
    EXPECT_EQ(s2, "world");
}