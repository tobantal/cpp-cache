#include <gtest/gtest.h>
#include <cache/expiration/PerKeyTTL.hpp>
#include <thread>
#include <chrono>

/**
 * @brief Тесты для PerKeyTTL
 * 
 * Проверяем:
 * - Индивидуальный TTL для каждого ключа
 * - Default TTL для ключей без явного TTL
 * - Бесконечный TTL (без default)
 * - setExpireAt для абсолютного времени
 * - updateTTL для обновления
 */

using namespace std::chrono_literals;

// ==================== Конструктор ====================

TEST(PerKeyTTLTest, ConstructorNoDefault) {
    PerKeyTTL<std::string> policy;
    
    EXPECT_FALSE(policy.getDefaultTTL().has_value());
}

TEST(PerKeyTTLTest, ConstructorWithDefault) {
    PerKeyTTL<std::string> policy(std::chrono::seconds(30));
    
    ASSERT_TRUE(policy.getDefaultTTL().has_value());
    EXPECT_EQ(policy.getDefaultTTL().value(), std::chrono::seconds(30));
}

TEST(PerKeyTTLTest, ConstructorWithSeconds) {
    PerKeyTTL<std::string> policy(60);  // 60 секунд
    
    ASSERT_TRUE(policy.getDefaultTTL().has_value());
    EXPECT_EQ(policy.getDefaultTTL().value(), std::chrono::seconds(60));
}

// ==================== Без default TTL ====================

TEST(PerKeyTTLTest, NoDefaultTTLMeansInfinite) {
    PerKeyTTL<std::string> policy;  // Без default
    
    policy.onInsert("key1");  // Без custom TTL
    
    // Ключ живёт вечно
    EXPECT_FALSE(policy.isExpired("key1"));
    EXPECT_FALSE(policy.hasExpiration("key1"));  // Не отслеживается
}

TEST(PerKeyTTLTest, CustomTTLWithoutDefault) {
    PerKeyTTL<std::string> policy;
    
    policy.onInsert("key1", std::chrono::milliseconds(50));
    
    EXPECT_FALSE(policy.isExpired("key1"));
    EXPECT_TRUE(policy.hasExpiration("key1"));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    
    EXPECT_TRUE(policy.isExpired("key1"));
}

// ==================== С default TTL ====================

TEST(PerKeyTTLTest, DefaultTTLApplied) {
    PerKeyTTL<std::string> policy(std::chrono::milliseconds(50));
    
    policy.onInsert("key1");  // Использует default TTL
    
    EXPECT_FALSE(policy.isExpired("key1"));
    EXPECT_TRUE(policy.hasExpiration("key1"));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    
    EXPECT_TRUE(policy.isExpired("key1"));
}

TEST(PerKeyTTLTest, CustomTTLOverridesDefault) {
    PerKeyTTL<std::string> policy(std::chrono::milliseconds(50));
    
    // Custom TTL = 200ms, больше чем default 50ms
    policy.onInsert("key1", std::chrono::milliseconds(200));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(70));
    
    // Не должен истечь — custom TTL ещё действует
    EXPECT_FALSE(policy.isExpired("key1"));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    // Теперь истёк
    EXPECT_TRUE(policy.isExpired("key1"));
}

// ==================== Разные TTL для разных ключей ====================

TEST(PerKeyTTLTest, DifferentTTLsForDifferentKeys) {
    PerKeyTTL<std::string> policy;
    
    policy.onInsert("short", std::chrono::milliseconds(30));
    policy.onInsert("long", std::chrono::milliseconds(200));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_TRUE(policy.isExpired("short"));
    EXPECT_FALSE(policy.isExpired("long"));
}

// ==================== timeToLive ====================

TEST(PerKeyTTLTest, TimeToLiveReturnsCorrectValue) {
    PerKeyTTL<std::string> policy;
    
    policy.onInsert("key1", std::chrono::milliseconds(100));
    
    auto ttl = policy.timeToLive("key1");
    
    ASSERT_TRUE(ttl.has_value());
    EXPECT_GT(ttl.value().count(), 0);
    EXPECT_LE(ttl.value(), std::chrono::milliseconds(100));
}

TEST(PerKeyTTLTest, TimeToLiveNulloptForInfinite) {
    PerKeyTTL<std::string> policy;
    
    policy.onInsert("key1");  // Без TTL
    
    auto ttl = policy.timeToLive("key1");
    
    EXPECT_FALSE(ttl.has_value());  // Бесконечный
}

TEST(PerKeyTTLTest, TimeToLiveZeroAfterExpired) {
    PerKeyTTL<std::string> policy;
    
    policy.onInsert("key1", std::chrono::milliseconds(30));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    auto ttl = policy.timeToLive("key1");
    
    ASSERT_TRUE(ttl.has_value());
    EXPECT_EQ(ttl.value(), std::chrono::milliseconds::zero());
}

// ==================== setExpireAt ====================

TEST(PerKeyTTLTest, SetExpireAt) {
    PerKeyTTL<std::string> policy;
    
    // Устанавливаем абсолютное время истечения
    auto expireTime = PerKeyTTL<std::string>::Clock::now() + 
                      std::chrono::milliseconds(50);
    
    policy.onInsert("key1");  // Бесконечный
    policy.setExpireAt("key1", expireTime);
    
    EXPECT_TRUE(policy.hasExpiration("key1"));
    EXPECT_FALSE(policy.isExpired("key1"));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    
    EXPECT_TRUE(policy.isExpired("key1"));
}

// ==================== updateTTL ====================

TEST(PerKeyTTLTest, UpdateTTLExtends) {
    PerKeyTTL<std::string> policy;
    
    policy.onInsert("key1", std::chrono::milliseconds(50));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    
    // Продлеваем TTL
    bool updated = policy.updateTTL("key1", std::chrono::milliseconds(100));
    
    EXPECT_TRUE(updated);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Не должен истечь — мы продлили
    EXPECT_FALSE(policy.isExpired("key1"));
}

TEST(PerKeyTTLTest, UpdateTTLNonExistent) {
    PerKeyTTL<std::string> policy;
    
    bool updated = policy.updateTTL("unknown", std::chrono::seconds(10));
    
    EXPECT_FALSE(updated);
}

// ==================== removeTTL ====================

TEST(PerKeyTTLTest, RemoveTTLMakesInfinite) {
    PerKeyTTL<std::string> policy;
    
    policy.onInsert("key1", std::chrono::milliseconds(50));
    EXPECT_TRUE(policy.hasExpiration("key1"));
    
    bool removed = policy.removeTTL("key1");
    
    EXPECT_TRUE(removed);
    EXPECT_FALSE(policy.hasExpiration("key1"));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    
    // Теперь живёт вечно
    EXPECT_FALSE(policy.isExpired("key1"));
}

// ==================== collectExpired ====================

TEST(PerKeyTTLTest, CollectExpiredWorks) {
    PerKeyTTL<std::string> policy;
    
    policy.onInsert("short1", std::chrono::milliseconds(30));
    policy.onInsert("short2", std::chrono::milliseconds(30));
    policy.onInsert("long1", std::chrono::milliseconds(200));
    policy.onInsert("infinite");  // Без TTL
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    auto expired = policy.collectExpired();
    
    EXPECT_EQ(expired.size(), 2);
    EXPECT_TRUE(std::find(expired.begin(), expired.end(), "short1") != expired.end());
    EXPECT_TRUE(std::find(expired.begin(), expired.end(), "short2") != expired.end());
}

// ==================== onRemove ====================

TEST(PerKeyTTLTest, RemoveStopsTracking) {
    PerKeyTTL<std::string> policy;
    
    policy.onInsert("key1", std::chrono::seconds(10));
    EXPECT_EQ(policy.trackedKeysCount(), 1);
    
    policy.onRemove("key1");
    EXPECT_EQ(policy.trackedKeysCount(), 0);
}

// ==================== clear ====================

TEST(PerKeyTTLTest, ClearRemovesAll) {
    PerKeyTTL<std::string> policy;
    
    policy.onInsert("key1", std::chrono::seconds(10));
    policy.onInsert("key2", std::chrono::seconds(20));
    policy.onInsert("key3", std::chrono::seconds(30));
    
    policy.clear();
    
    EXPECT_EQ(policy.trackedKeysCount(), 0);
}

// ==================== setDefaultTTL ====================

TEST(PerKeyTTLTest, SetDefaultTTL) {
    PerKeyTTL<std::string> policy;
    
    policy.setDefaultTTL(std::chrono::seconds(60));
    
    ASSERT_TRUE(policy.getDefaultTTL().has_value());
    EXPECT_EQ(policy.getDefaultTTL().value(), std::chrono::seconds(60));
}

TEST(PerKeyTTLTest, SetDefaultTTLToNullopt) {
    PerKeyTTL<std::string> policy(std::chrono::seconds(30));
    
    policy.setDefaultTTL(std::nullopt);
    
    EXPECT_FALSE(policy.getDefaultTTL().has_value());
}

// ==================== Различные типы ключей ====================

TEST(PerKeyTTLTest, WorksWithIntKeys) {
    PerKeyTTL<int> policy;
    
    policy.onInsert(1, std::chrono::milliseconds(50));
    policy.onInsert(2, std::chrono::milliseconds(200));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    
    EXPECT_TRUE(policy.isExpired(1));
    EXPECT_FALSE(policy.isExpired(2));
}