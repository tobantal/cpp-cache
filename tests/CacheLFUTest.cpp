#include <gtest/gtest.h>
#include <cache/Cache.hpp>
#include <cache/policies/LFUPolicy.hpp>
#include <cache/listeners/StatsListener.hpp>
#include <string>

/**
 * @brief Тесты интеграции Cache с LFUPolicy
 * 
 * Проверяем:
 * - Корректность вытеснения по LFU
 * - Работу со StatsListener
 * - Смену политики с LRU на LFU
 * - Граничные случаи
 */

// ==================== Вспомогательные функции ====================

/**
 * @brief Создаёт кэш с LFU политикой
 */
template<typename K, typename V>
Cache<K, V> makeLFUCache(size_t capacity) {
    return Cache<K, V>(capacity, std::make_unique<LFUPolicy<K>>());
}

// ==================== Базовые операции ====================

/**
 * @brief Put и Get работают с LFU политикой
 */
TEST(CacheLFUTest, BasicPutAndGet) {
    auto cache = makeLFUCache<std::string, int>(10);
    
    cache.put("key1", 42);
    
    auto result = cache.get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 42);
}

/**
 * @brief Множественные элементы
 */
TEST(CacheLFUTest, MultiplePuts) {
    auto cache = makeLFUCache<std::string, int>(10);
    
    cache.put("a", 1);
    cache.put("b", 2);
    cache.put("c", 3);
    
    EXPECT_EQ(cache.size(), 3);
    EXPECT_EQ(cache.get("a").value(), 1);
    EXPECT_EQ(cache.get("b").value(), 2);
    EXPECT_EQ(cache.get("c").value(), 3);
}

// ==================== Вытеснение по LFU ====================

/**
 * @brief Вытесняется элемент с наименьшей частотой
 */
TEST(CacheLFUTest, EvictsLeastFrequentlyUsed) {
    auto cache = makeLFUCache<std::string, int>(3);
    
    cache.put("A", 1);  // A: freq=1
    cache.put("B", 2);  // B: freq=1
    cache.put("C", 3);  // C: freq=1
    
    // Увеличиваем частоту A и B
    cache.get("A");  // A: freq=2
    cache.get("A");  // A: freq=3
    cache.get("B");  // B: freq=2
    // C остаётся с freq=1
    
    cache.put("D", 4);  // Должен вытеснить C (freq=1)
    
    EXPECT_EQ(cache.size(), 3);
    EXPECT_TRUE(cache.contains("A"));
    EXPECT_TRUE(cache.contains("B"));
    EXPECT_FALSE(cache.contains("C"));  // C вытеснен
    EXPECT_TRUE(cache.contains("D"));
}

/**
 * @brief При равной частоте вытесняется LRU
 */
TEST(CacheLFUTest, EvictsLRUOnEqualFrequency) {
    auto cache = makeLFUCache<std::string, int>(3);
    
    cache.put("A", 1);  // A: freq=1, oldest
    cache.put("B", 2);  // B: freq=1
    cache.put("C", 3);  // C: freq=1, newest
    // Все с freq=1, LRU = A
    
    cache.put("D", 4);  // Должен вытеснить A (LRU среди freq=1)
    
    EXPECT_FALSE(cache.contains("A"));  // A вытеснен
    EXPECT_TRUE(cache.contains("B"));
    EXPECT_TRUE(cache.contains("C"));
    EXPECT_TRUE(cache.contains("D"));
}

/**
 * @brief Update не вызывает вытеснение и увеличивает частоту
 */
TEST(CacheLFUTest, UpdateDoesNotEvict) {
    auto cache = makeLFUCache<std::string, int>(3);
    
    cache.put("A", 1);
    cache.put("B", 2);
    cache.put("C", 3);
    
    cache.put("A", 100);  // Update, не insert
    
    EXPECT_EQ(cache.size(), 3);
    EXPECT_EQ(cache.get("A").value(), 100);
    
    // A теперь имеет более высокую частоту
    cache.put("D", 4);  // Вытеснит B или C (freq=1)
    
    EXPECT_TRUE(cache.contains("A"));  // A не вытеснен
}

/**
 * @brief Последовательность вытеснений следует LFU логике
 */
TEST(CacheLFUTest, EvictionSequence) {
    auto cache = makeLFUCache<std::string, int>(2);
    
    cache.put("A", 1);  // A: 1
    cache.put("B", 2);  // B: 1
    
    cache.get("A");     // A: 2
    
    cache.put("C", 3);  // Вытеснит B (freq=1)
    
    EXPECT_TRUE(cache.contains("A"));
    EXPECT_FALSE(cache.contains("B"));
    EXPECT_TRUE(cache.contains("C"));
    
    cache.get("A");     // A: 3
    
    cache.put("D", 4);  // Вытеснит C (freq=1)
    
    EXPECT_TRUE(cache.contains("A"));
    EXPECT_FALSE(cache.contains("C"));
    EXPECT_TRUE(cache.contains("D"));
}

// ==================== Статистика ====================

/**
 * @brief StatsListener работает с LFU кэшем
 */
TEST(CacheLFUTest, StatsListenerWorks) {
    auto cache = makeLFUCache<std::string, int>(3);
    auto stats = std::make_shared<StatsListener<std::string, int>>();
    cache.addListener(stats);
    
    cache.put("A", 1);
    cache.put("B", 2);
    cache.put("C", 3);
    
    cache.get("A");      // Hit
    cache.get("A");      // Hit
    cache.get("missing"); // Miss
    
    EXPECT_EQ(stats->inserts(), 3);
    EXPECT_EQ(stats->hits(), 2);
    EXPECT_EQ(stats->misses(), 1);
}

/**
 * @brief Подсчёт вытеснений с LFU
 */
TEST(CacheLFUTest, CountsEvictions) {
    auto cache = makeLFUCache<std::string, int>(2);
    auto stats = std::make_shared<StatsListener<std::string, int>>();
    cache.addListener(stats);
    
    cache.put("A", 1);
    cache.put("B", 2);
    cache.put("C", 3);  // Evicts one
    cache.put("D", 4);  // Evicts one
    
    EXPECT_EQ(stats->evictions(), 2);
}

// ==================== Граничные случаи ====================

/**
 * @brief Кэш размера 1 с LFU
 */
TEST(CacheLFUTest, CapacityOne) {
    auto cache = makeLFUCache<std::string, int>(1);
    
    cache.put("A", 1);
    cache.get("A");     // freq=2
    cache.get("A");     // freq=3
    
    cache.put("B", 2);  // Вытеснит A несмотря на высокую частоту
    
    EXPECT_FALSE(cache.contains("A"));
    EXPECT_TRUE(cache.contains("B"));
}

/**
 * @brief Частые обращения к одному элементу
 */
TEST(CacheLFUTest, FrequentAccessToOneElement) {
    auto cache = makeLFUCache<std::string, int>(3);
    
    cache.put("hot", 1);
    cache.put("warm", 2);
    cache.put("cold", 3);
    
    // "hot" используется очень часто
    for (int i = 0; i < 100; ++i) {
        cache.get("hot");
    }
    
    // "warm" используется иногда
    for (int i = 0; i < 10; ++i) {
        cache.get("warm");
    }
    
    // "cold" не используется
    
    cache.put("new", 4);  // Должен вытеснить "cold"
    
    EXPECT_TRUE(cache.contains("hot"));
    EXPECT_TRUE(cache.contains("warm"));
    EXPECT_FALSE(cache.contains("cold"));  // Вытеснен
    EXPECT_TRUE(cache.contains("new"));
}

/**
 * @brief Clear и повторное использование
 */
TEST(CacheLFUTest, ClearAndReuse) {
    auto cache = makeLFUCache<std::string, int>(3);
    
    cache.put("A", 1);
    cache.get("A");
    cache.get("A");
    
    cache.clear();
    
    EXPECT_EQ(cache.size(), 0);
    
    // После clear должен работать как новый
    cache.put("B", 2);
    cache.put("C", 3);
    cache.put("D", 4);
    
    cache.put("E", 5);  // Вытеснит B (LRU среди freq=1)
    
    EXPECT_FALSE(cache.contains("B"));
}

// ==================== Сложные сценарии ====================

/**
 * @brief Реалистичный сценарий: "горячие" и "холодные" данные
 */
TEST(CacheLFUTest, HotAndColdData) {
    auto cache = makeLFUCache<std::string, int>(5);
    auto stats = std::make_shared<StatsListener<std::string, int>>();
    cache.addListener(stats);
    
    // Фаза 1: Загрузка данных
    cache.put("user:1", 100);   // "Горячий" пользователь
    cache.put("user:2", 200);   // "Холодный" пользователь
    cache.put("config:1", 300); // Конфигурация (редко меняется)
    cache.put("session:1", 400);
    cache.put("session:2", 500);
    
    // Фаза 2: Активная работа
    // "Горячие" данные запрашиваются часто
    for (int i = 0; i < 20; ++i) {
        cache.get("user:1");
        cache.get("config:1");
    }
    
    // Фаза 3: Новые данные вытесняют "холодные"
    cache.put("session:3", 600);  // Вытеснит user:2 или session:1/2
    
    EXPECT_TRUE(cache.contains("user:1"));   // Горячий — остался
    EXPECT_TRUE(cache.contains("config:1")); // Горячий — остался
    
    // Hit rate должен быть высоким из-за частых обращений к "горячим" данным
    EXPECT_GT(stats->hitRate(), 0.8);
}

/**
 * @brief Смена паттерна доступа
 */
TEST(CacheLFUTest, ChangingAccessPattern) {
    auto cache = makeLFUCache<std::string, int>(3);
    
    cache.put("A", 1);
    cache.put("B", 2);
    cache.put("C", 3);
    
    // Фаза 1: A — горячий
    for (int i = 0; i < 10; ++i) {
        cache.get("A");
    }
    
    // Фаза 2: Паттерн изменился, теперь B — горячий
    // Но A всё ещё имеет накопленную частоту
    for (int i = 0; i < 5; ++i) {
        cache.get("B");
    }
    
    // A: freq=11, B: freq=6, C: freq=1
    cache.put("D", 4);  // Вытеснит C
    
    EXPECT_TRUE(cache.contains("A"));
    EXPECT_TRUE(cache.contains("B"));
    EXPECT_FALSE(cache.contains("C"));  // Самая низкая частота
}

// ==================== Типы данных ====================

/**
 * @brief LFU с int ключами
 */
TEST(CacheLFUTest, IntKeys) {
    auto cache = makeLFUCache<int, std::string>(3);
    
    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");
    
    cache.get(1);
    cache.get(1);
    
    cache.put(4, "four");  // Вытеснит 2 или 3
    
    EXPECT_TRUE(cache.contains(1));
    EXPECT_TRUE(cache.contains(4));
}

/**
 * @brief LFU со сложными значениями
 */
TEST(CacheLFUTest, ComplexValues) {
    struct UserData {
        std::string name;
        int age;
        
        bool operator==(const UserData& other) const {
            return name == other.name && age == other.age;
        }
    };
    
    auto cache = makeLFUCache<std::string, UserData>(3);
    
    cache.put("user1", {"Alice", 30});
    cache.put("user2", {"Bob", 25});
    
    auto result = cache.get("user1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().name, "Alice");
    EXPECT_EQ(result.value().age, 30);
}