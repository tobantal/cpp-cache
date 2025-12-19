#include <gtest/gtest.h>
#include "cache/Cache.hpp"
#include "cache/eviction/LRUPolicy.hpp"
#include "cache/listeners/LoggingListener.hpp"
#include "cache/listeners/StatsListener.hpp"
#include <sstream>

/**
 * @brief Тесты для слушателей
 * 
 * Проверяем:
 * - StatsListener корректно считает статистику
 * - LoggingListener выводит сообщения
 * - Множественные слушатели работают вместе
 * - Удаление слушателей
 */

// ==================== Вспомогательные функции ====================

template<typename K, typename V>
Cache<K, V> makeLRUCache(size_t capacity) {
    return Cache<K, V>(capacity, std::make_unique<LRUPolicy<K>>());
}

// ==================== StatsListener ====================

TEST(StatsListenerTest, InitiallyZero) {
    StatsListener<std::string, int> stats;
    
    EXPECT_EQ(stats.hits(), 0);
    EXPECT_EQ(stats.misses(), 0);
    EXPECT_EQ(stats.inserts(), 0);
    EXPECT_EQ(stats.updates(), 0);
    EXPECT_EQ(stats.evictions(), 0);
    EXPECT_EQ(stats.removes(), 0);
    EXPECT_EQ(stats.clears(), 0);
    EXPECT_DOUBLE_EQ(stats.hitRate(), 0.0);
}

TEST(StatsListenerTest, CountsHitsAndMisses) {
    auto cache = makeLRUCache<std::string, int>(10);
    auto stats = std::make_shared<StatsListener<std::string, int>>();
    cache.addListener(stats);
    
    cache.put("key1", 42);
    cache.get("key1");      // Hit
    cache.get("key1");      // Hit
    cache.get("missing");   // Miss
    
    EXPECT_EQ(stats->hits(), 2);
    EXPECT_EQ(stats->misses(), 1);
    EXPECT_EQ(stats->totalRequests(), 3);
}

TEST(StatsListenerTest, HitRateCalculation) {
    auto cache = makeLRUCache<std::string, int>(10);
    auto stats = std::make_shared<StatsListener<std::string, int>>();
    cache.addListener(stats);
    
    cache.put("key1", 42);
    cache.get("key1");      // Hit
    cache.get("key1");      // Hit
    cache.get("key1");      // Hit
    cache.get("missing");   // Miss
    
    // 3 hits, 1 miss = 75% hit rate
    EXPECT_DOUBLE_EQ(stats->hitRate(), 0.75);
}

TEST(StatsListenerTest, CountsInserts) {
    auto cache = makeLRUCache<std::string, int>(10);
    auto stats = std::make_shared<StatsListener<std::string, int>>();
    cache.addListener(stats);
    
    cache.put("a", 1);
    cache.put("b", 2);
    cache.put("c", 3);
    
    EXPECT_EQ(stats->inserts(), 3);
}

TEST(StatsListenerTest, CountsUpdates) {
    auto cache = makeLRUCache<std::string, int>(10);
    auto stats = std::make_shared<StatsListener<std::string, int>>();
    cache.addListener(stats);
    
    cache.put("key1", 1);   // Insert
    cache.put("key1", 2);   // Update
    cache.put("key1", 3);   // Update
    
    EXPECT_EQ(stats->inserts(), 1);
    EXPECT_EQ(stats->updates(), 2);
}

TEST(StatsListenerTest, CountsEvictions) {
    auto cache = makeLRUCache<std::string, int>(2);
    auto stats = std::make_shared<StatsListener<std::string, int>>();
    cache.addListener(stats);
    
    cache.put("a", 1);
    cache.put("b", 2);
    cache.put("c", 3);  // Evicts "a"
    cache.put("d", 4);  // Evicts "b"
    
    EXPECT_EQ(stats->evictions(), 2);
}

TEST(StatsListenerTest, CountsRemoves) {
    auto cache = makeLRUCache<std::string, int>(10);
    auto stats = std::make_shared<StatsListener<std::string, int>>();
    cache.addListener(stats);
    
    cache.put("a", 1);
    cache.put("b", 2);
    cache.remove("a");
    cache.remove("nonexistent");  // Не должен увеличить счётчик
    
    EXPECT_EQ(stats->removes(), 1);
}

TEST(StatsListenerTest, CountsClears) {
    auto cache = makeLRUCache<std::string, int>(10);
    auto stats = std::make_shared<StatsListener<std::string, int>>();
    cache.addListener(stats);
    
    cache.put("a", 1);
    cache.clear();
    cache.put("b", 2);
    cache.clear();
    
    EXPECT_EQ(stats->clears(), 2);
}

TEST(StatsListenerTest, Reset) {
    StatsListener<std::string, int> stats;
    
    stats.onHit("key");
    stats.onMiss("key");
    stats.onInsert("key", 42);
    
    stats.reset();
    
    EXPECT_EQ(stats.hits(), 0);
    EXPECT_EQ(stats.misses(), 0);
    EXPECT_EQ(stats.inserts(), 0);
}

// ==================== LoggingListener ====================

TEST(LoggingListenerTest, LogsHit) {
    std::ostringstream oss;
    LoggingListener<std::string, int> logger("Test", oss);
    
    logger.onHit("key1");
    
    EXPECT_NE(oss.str().find("HIT"), std::string::npos);
    EXPECT_NE(oss.str().find("key1"), std::string::npos);
}

TEST(LoggingListenerTest, LogsMiss) {
    std::ostringstream oss;
    LoggingListener<std::string, int> logger("Test", oss);
    
    logger.onMiss("missing");
    
    EXPECT_NE(oss.str().find("MISS"), std::string::npos);
    EXPECT_NE(oss.str().find("missing"), std::string::npos);
}

TEST(LoggingListenerTest, LogsInsert) {
    std::ostringstream oss;
    LoggingListener<std::string, int> logger("Test", oss);
    
    logger.onInsert("key1", 42);
    
    EXPECT_NE(oss.str().find("INSERT"), std::string::npos);
    EXPECT_NE(oss.str().find("key1"), std::string::npos);
    EXPECT_NE(oss.str().find("42"), std::string::npos);
}

TEST(LoggingListenerTest, LogsEvict) {
    std::ostringstream oss;
    LoggingListener<std::string, int> logger("Test", oss);
    
    logger.onEvict("victim", 123);
    
    EXPECT_NE(oss.str().find("EVICT"), std::string::npos);
    EXPECT_NE(oss.str().find("victim"), std::string::npos);
}

TEST(LoggingListenerTest, CustomPrefix) {
    std::ostringstream oss;
    LoggingListener<std::string, int> logger("MyCache", oss);
    
    logger.onHit("key");
    
    EXPECT_NE(oss.str().find("[MyCache]"), std::string::npos);
}

TEST(LoggingListenerTest, IntegrationWithCache) {
    std::ostringstream oss;
    auto cache = makeLRUCache<std::string, int>(10);
    auto logger = std::make_shared<LoggingListener<std::string, int>>("Cache", oss);
    cache.addListener(logger);
    
    cache.put("key1", 42);
    cache.get("key1");
    cache.get("missing");
    
    std::string output = oss.str();
    EXPECT_NE(output.find("INSERT"), std::string::npos);
    EXPECT_NE(output.find("HIT"), std::string::npos);
    EXPECT_NE(output.find("MISS"), std::string::npos);
}

// ==================== Множественные слушатели ====================

TEST(ListenersTest, MultipleListeners) {
    auto cache = makeLRUCache<std::string, int>(10);
    auto stats = std::make_shared<StatsListener<std::string, int>>();
    std::ostringstream oss;
    auto logger = std::make_shared<LoggingListener<std::string, int>>("Test", oss);
    
    cache.addListener(stats);
    cache.addListener(logger);
    
    cache.put("key1", 42);
    cache.get("key1");
    
    // Оба слушателя получили события
    EXPECT_EQ(stats->inserts(), 1);
    EXPECT_EQ(stats->hits(), 1);
    EXPECT_NE(oss.str().find("INSERT"), std::string::npos);
    EXPECT_NE(oss.str().find("HIT"), std::string::npos);
}

TEST(ListenersTest, RemoveListener) {
    auto cache = makeLRUCache<std::string, int>(10);
    auto stats = std::make_shared<StatsListener<std::string, int>>();
    
    cache.addListener(stats);
    cache.put("a", 1);  // Stats: 1 insert
    
    cache.removeListener(stats);
    cache.put("b", 2);  // Stats не должен обновиться
    
    EXPECT_EQ(stats->inserts(), 1);  // Всё ещё 1
}

TEST(ListenersTest, NoListenersNoCrash) {
    // Кэш без слушателей должен работать нормально
    auto cache = makeLRUCache<std::string, int>(10);
    
    cache.put("key1", 42);
    cache.get("key1");
    cache.get("missing");
    cache.remove("key1");
    cache.clear();
    
    EXPECT_EQ(cache.size(), 0);
}

TEST(ListenersTest, AddNullListenerIgnored) {
    auto cache = makeLRUCache<std::string, int>(10);
    
    cache.addListener(nullptr);  // Не должно падать
    cache.put("key1", 42);       // Не должно падать
    
    EXPECT_EQ(cache.size(), 1);
}