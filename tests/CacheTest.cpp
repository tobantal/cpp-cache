#include <gtest/gtest.h>
#include <cache/Cache.hpp>
#include <cache/eviction/LRUPolicy.hpp>
#include <string>

/**
 * @brief Тесты для Cache
 * 
 * Проверяем:
 * - Базовые операции (get, put, remove, clear)
 * - Вытеснение при переполнении
 * - Смена политики в runtime
 * - Граничные случаи
 */

// ==================== Вспомогательные функции ====================

/**
 * @brief Создаёт кэш с LRU политикой
 */
template<typename K, typename V>
Cache<K, V> makeLRUCache(size_t capacity) {
    return Cache<K, V>(capacity, std::make_unique<LRUPolicy<K>>());
}

// ==================== Конструктор ====================

TEST(CacheTest, ConstructorThrowsOnZeroCapacity) {
    EXPECT_THROW(
        (Cache<std::string, int>(0, std::make_unique<LRUPolicy<std::string>>())),
        std::invalid_argument
    );
}

TEST(CacheTest, ConstructorThrowsOnNullPolicy) {
    EXPECT_THROW(
        (Cache<std::string, int>(100, nullptr)),
        std::invalid_argument
    );
}

TEST(CacheTest, EmptyOnCreate) {
    auto cache = makeLRUCache<std::string, int>(10);
    EXPECT_EQ(cache.size(), 0);
    EXPECT_EQ(cache.capacity(), 10);
}

// ==================== Put и Get ====================

TEST(CacheTest, PutAndGet) {
    auto cache = makeLRUCache<std::string, int>(10);
    
    cache.put("key1", 42);
    
    auto result = cache.get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 42);
}

TEST(CacheTest, GetNonExistentReturnsNullopt) {
    auto cache = makeLRUCache<std::string, int>(10);
    
    auto result = cache.get("nonexistent");
    
    EXPECT_FALSE(result.has_value());
}

TEST(CacheTest, PutUpdatesExistingValue) {
    auto cache = makeLRUCache<std::string, int>(10);
    
    cache.put("key1", 42);
    cache.put("key1", 100);  // Update
    
    auto result = cache.get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 100);
    EXPECT_EQ(cache.size(), 1);  // Размер не изменился
}

TEST(CacheTest, MultiplePuts) {
    auto cache = makeLRUCache<std::string, int>(10);
    
    cache.put("a", 1);
    cache.put("b", 2);
    cache.put("c", 3);
    
    EXPECT_EQ(cache.size(), 3);
    EXPECT_EQ(cache.get("a").value(), 1);
    EXPECT_EQ(cache.get("b").value(), 2);
    EXPECT_EQ(cache.get("c").value(), 3);
}

// ==================== Contains ====================

TEST(CacheTest, ContainsReturnsTrueForExisting) {
    auto cache = makeLRUCache<std::string, int>(10);
    cache.put("key1", 42);
    
    EXPECT_TRUE(cache.contains("key1"));
}

TEST(CacheTest, ContainsReturnsFalseForNonExisting) {
    auto cache = makeLRUCache<std::string, int>(10);
    
    EXPECT_FALSE(cache.contains("nonexistent"));
}

// ==================== Remove ====================

TEST(CacheTest, RemoveExistingKey) {
    auto cache = makeLRUCache<std::string, int>(10);
    cache.put("key1", 42);
    
    bool removed = cache.remove("key1");
    
    EXPECT_TRUE(removed);
    EXPECT_FALSE(cache.contains("key1"));
    EXPECT_EQ(cache.size(), 0);
}

TEST(CacheTest, RemoveNonExistingKeyReturnsFalse) {
    auto cache = makeLRUCache<std::string, int>(10);
    
    bool removed = cache.remove("nonexistent");
    
    EXPECT_FALSE(removed);
}

// ==================== Clear ====================

TEST(CacheTest, ClearRemovesAllElements) {
    auto cache = makeLRUCache<std::string, int>(10);
    cache.put("a", 1);
    cache.put("b", 2);
    cache.put("c", 3);
    
    cache.clear();
    
    EXPECT_EQ(cache.size(), 0);
    EXPECT_FALSE(cache.contains("a"));
    EXPECT_FALSE(cache.contains("b"));
    EXPECT_FALSE(cache.contains("c"));
}

TEST(CacheTest, ClearOnEmptyCache) {
    auto cache = makeLRUCache<std::string, int>(10);
    
    cache.clear();  // Не должно падать
    
    EXPECT_EQ(cache.size(), 0);
}

// ==================== Вытеснение ====================

TEST(CacheTest, EvictsWhenFull) {
    // Кэш на 3 элемента
    auto cache = makeLRUCache<std::string, int>(3);
    
    cache.put("A", 1);
    cache.put("B", 2);
    cache.put("C", 3);
    // Кэш полон: [C, B, A], LRU = A
    
    cache.put("D", 4);  // Должен вытеснить A
    
    EXPECT_EQ(cache.size(), 3);
    EXPECT_FALSE(cache.contains("A"));  // A вытеснен
    EXPECT_TRUE(cache.contains("B"));
    EXPECT_TRUE(cache.contains("C"));
    EXPECT_TRUE(cache.contains("D"));
}

TEST(CacheTest, EvictsLRUElement) {
    auto cache = makeLRUCache<std::string, int>(3);
    
    cache.put("A", 1);
    cache.put("B", 2);
    cache.put("C", 3);
    
    // Доступ к A делает его MRU
    cache.get("A");
    // Теперь порядок: [A, C, B], LRU = B
    
    cache.put("D", 4);  // Должен вытеснить B
    
    EXPECT_TRUE(cache.contains("A"));
    EXPECT_FALSE(cache.contains("B"));  // B вытеснен
    EXPECT_TRUE(cache.contains("C"));
    EXPECT_TRUE(cache.contains("D"));
}

TEST(CacheTest, UpdateDoesNotCauseEviction) {
    auto cache = makeLRUCache<std::string, int>(3);
    
    cache.put("A", 1);
    cache.put("B", 2);
    cache.put("C", 3);
    
    cache.put("A", 100);  // Update, не insert
    
    EXPECT_EQ(cache.size(), 3);
    EXPECT_EQ(cache.get("A").value(), 100);
}

TEST(CacheTest, EvictionSequence) {
    // Проверяем последовательность вытеснений
    auto cache = makeLRUCache<int, int>(2);
    
    cache.put(1, 100);
    cache.put(2, 200);
    cache.put(3, 300);  // Вытесняет 1
    
    EXPECT_FALSE(cache.contains(1));
    
    cache.put(4, 400);  // Вытесняет 2
    
    EXPECT_FALSE(cache.contains(2));
    EXPECT_TRUE(cache.contains(3));
    EXPECT_TRUE(cache.contains(4));
}

// ==================== Смена политики ====================

TEST(CacheTest, SetEvictionPolicyThrowsOnNull) {
    auto cache = makeLRUCache<std::string, int>(10);
    
    EXPECT_THROW(cache.setEvictionPolicy(nullptr), std::invalid_argument);
}

TEST(CacheTest, SetEvictionPolicyPreservesData) {
    auto cache = makeLRUCache<std::string, int>(10);
    cache.put("A", 1);
    cache.put("B", 2);
    cache.put("C", 3);
    
    // Меняем политику на новый LRU
    cache.setEvictionPolicy(std::make_unique<LRUPolicy<std::string>>());
    
    // Данные должны сохраниться
    EXPECT_EQ(cache.size(), 3);
    EXPECT_EQ(cache.get("A").value(), 1);
    EXPECT_EQ(cache.get("B").value(), 2);
    EXPECT_EQ(cache.get("C").value(), 3);
}

TEST(CacheTest, SetEvictionPolicyWorksCorrectly) {
    auto cache = makeLRUCache<std::string, int>(3);
    cache.put("A", 1);
    cache.put("B", 2);
    cache.put("C", 3);
    
    // Меняем политику — порядок сбрасывается
    cache.setEvictionPolicy(std::make_unique<LRUPolicy<std::string>>());
    
    // Добавляем новый элемент — какой-то из старых вытеснится
    // (порядок после setEvictionPolicy зависит от порядка итерации unordered_map)
    cache.put("D", 4);
    
    EXPECT_EQ(cache.size(), 3);
    EXPECT_TRUE(cache.contains("D"));
}

// ==================== Граничные случаи ====================

TEST(CacheTest, CapacityOne) {
    auto cache = makeLRUCache<std::string, int>(1);
    
    cache.put("A", 1);
    EXPECT_EQ(cache.get("A").value(), 1);
    
    cache.put("B", 2);  // Вытесняет A
    
    EXPECT_FALSE(cache.contains("A"));
    EXPECT_EQ(cache.get("B").value(), 2);
}

TEST(CacheTest, WorksWithComplexValues) {
    auto cache = makeLRUCache<int, std::string>(10);
    
    cache.put(1, "hello");
    cache.put(2, "world");
    
    EXPECT_EQ(cache.get(1).value(), "hello");
    EXPECT_EQ(cache.get(2).value(), "world");
}