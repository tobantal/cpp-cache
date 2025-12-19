#include <gtest/gtest.h>
#include <cache/expiration/GlobalTTL.hpp>
#include <thread>
#include <chrono>

/**
 * @brief Тесты для GlobalTTL
 * 
 * Проверяем:
 * - Базовые операции (insert, remove, clear)
 * - Корректность проверки истечения
 * - timeToLive() возвращает правильные значения
 * - collectExpired() собирает просроченные ключи
 */

using namespace std::chrono_literals;

// ==================== Конструктор ====================

TEST(GlobalTTLTest, ConstructorWithDuration) {
    GlobalTTL<std::string> policy(std::chrono::seconds(10));
    EXPECT_EQ(policy.getGlobalTTL(), std::chrono::seconds(10));
}

TEST(GlobalTTLTest, ConstructorWithSeconds) {
    GlobalTTL<std::string> policy(30);  // 30 секунд
    EXPECT_EQ(policy.getGlobalTTL(), std::chrono::seconds(30));
}

TEST(GlobalTTLTest, ConstructorThrowsOnZeroTTL) {
    EXPECT_THROW(
        GlobalTTL<std::string>(std::chrono::seconds(0)),
        std::invalid_argument
    );
}

TEST(GlobalTTLTest, ConstructorThrowsOnNegativeTTL) {
    EXPECT_THROW(
        GlobalTTL<std::string>(std::chrono::seconds(-1)),
        std::invalid_argument
    );
}

// ==================== isExpired ====================

TEST(GlobalTTLTest, NotExpiredImmediately) {
    GlobalTTL<std::string> policy(std::chrono::seconds(10));
    
    policy.onInsert("key1");
    
    EXPECT_FALSE(policy.isExpired("key1"));
}

TEST(GlobalTTLTest, ExpiredAfterTTL) {
    // Используем очень короткий TTL для теста
    GlobalTTL<std::string> policy(std::chrono::milliseconds(50));
    
    policy.onInsert("key1");
    EXPECT_FALSE(policy.isExpired("key1"));
    
    // Ждём истечения
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    
    EXPECT_TRUE(policy.isExpired("key1"));
}

TEST(GlobalTTLTest, NotExpiredJustBeforeTTL) {
    GlobalTTL<std::string> policy(std::chrono::milliseconds(100));
    
    policy.onInsert("key1");
    
    // Ждём почти до истечения
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_FALSE(policy.isExpired("key1"));
}

TEST(GlobalTTLTest, UnknownKeyNotExpired) {
    GlobalTTL<std::string> policy(std::chrono::seconds(10));
    
    // Ключ не зарегистрирован
    EXPECT_FALSE(policy.isExpired("unknown"));
}

// ==================== onRemove ====================

TEST(GlobalTTLTest, RemoveStopsTracking) {
    GlobalTTL<std::string> policy(std::chrono::seconds(10));
    
    policy.onInsert("key1");
    EXPECT_EQ(policy.trackedKeysCount(), 1);
    
    policy.onRemove("key1");
    EXPECT_EQ(policy.trackedKeysCount(), 0);
}

TEST(GlobalTTLTest, RemoveNonExistentKeyDoesNothing) {
    GlobalTTL<std::string> policy(std::chrono::seconds(10));
    
    policy.onRemove("nonexistent");  // Не должно падать
    
    EXPECT_EQ(policy.trackedKeysCount(), 0);
}

// ==================== clear ====================

TEST(GlobalTTLTest, ClearRemovesAllTracking) {
    GlobalTTL<std::string> policy(std::chrono::seconds(10));
    
    policy.onInsert("key1");
    policy.onInsert("key2");
    policy.onInsert("key3");
    EXPECT_EQ(policy.trackedKeysCount(), 3);
    
    policy.clear();
    
    EXPECT_EQ(policy.trackedKeysCount(), 0);
}

// ==================== timeToLive ====================

TEST(GlobalTTLTest, TimeToLiveReturnsPositive) {
    GlobalTTL<std::string> policy(std::chrono::seconds(10));
    
    policy.onInsert("key1");
    
    auto ttl = policy.timeToLive("key1");
    ASSERT_TRUE(ttl.has_value());
    EXPECT_GT(ttl.value().count(), 0);
    EXPECT_LE(ttl.value(), std::chrono::seconds(10));
}

TEST(GlobalTTLTest, TimeToLiveDecreasesOverTime) {
    GlobalTTL<std::string> policy(std::chrono::milliseconds(200));
    
    policy.onInsert("key1");
    auto ttl1 = policy.timeToLive("key1");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    auto ttl2 = policy.timeToLive("key1");
    
    ASSERT_TRUE(ttl1.has_value());
    ASSERT_TRUE(ttl2.has_value());
    EXPECT_LT(ttl2.value(), ttl1.value());
}

TEST(GlobalTTLTest, TimeToLiveReturnsZeroAfterExpired) {
    GlobalTTL<std::string> policy(std::chrono::milliseconds(30));
    
    policy.onInsert("key1");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    auto ttl = policy.timeToLive("key1");
    ASSERT_TRUE(ttl.has_value());
    EXPECT_EQ(ttl.value(), std::chrono::milliseconds::zero());
}

TEST(GlobalTTLTest, TimeToLiveReturnsNulloptForUnknown) {
    GlobalTTL<std::string> policy(std::chrono::seconds(10));
    
    auto ttl = policy.timeToLive("unknown");
    
    EXPECT_FALSE(ttl.has_value());
}

// ==================== collectExpired ====================

TEST(GlobalTTLTest, CollectExpiredReturnsEmpty) {
    GlobalTTL<std::string> policy(std::chrono::seconds(10));
    
    policy.onInsert("key1");
    policy.onInsert("key2");
    
    auto expired = policy.collectExpired();
    
    EXPECT_TRUE(expired.empty());
}

TEST(GlobalTTLTest, CollectExpiredReturnsExpiredKeys) {
    GlobalTTL<std::string> policy(std::chrono::milliseconds(30));
    
    policy.onInsert("key1");
    policy.onInsert("key2");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    auto expired = policy.collectExpired();
    
    EXPECT_EQ(expired.size(), 2);
    EXPECT_TRUE(std::find(expired.begin(), expired.end(), "key1") != expired.end());
    EXPECT_TRUE(std::find(expired.begin(), expired.end(), "key2") != expired.end());
}

TEST(GlobalTTLTest, CollectExpiredMixedState) {
    GlobalTTL<std::string> policy(std::chrono::milliseconds(50));
    
    policy.onInsert("old1");
    policy.onInsert("old2");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    
    // Добавляем новые ключи после истечения старых
    policy.onInsert("new1");
    
    auto expired = policy.collectExpired();
    
    EXPECT_EQ(expired.size(), 2);  // Только old1 и old2
    EXPECT_TRUE(std::find(expired.begin(), expired.end(), "new1") == expired.end());
}

// ==================== setGlobalTTL ====================

TEST(GlobalTTLTest, SetGlobalTTL) {
    GlobalTTL<std::string> policy(std::chrono::seconds(10));
    
    policy.setGlobalTTL(std::chrono::seconds(30));
    
    EXPECT_EQ(policy.getGlobalTTL(), std::chrono::seconds(30));
}

TEST(GlobalTTLTest, SetGlobalTTLThrowsOnInvalid) {
    GlobalTTL<std::string> policy(std::chrono::seconds(10));
    
    EXPECT_THROW(
        policy.setGlobalTTL(std::chrono::seconds(0)),
        std::invalid_argument
    );
}

TEST(GlobalTTLTest, SetGlobalTTLAffectsNewKeysOnly) {
    GlobalTTL<std::string> policy(std::chrono::milliseconds(100));
    
    policy.onInsert("old_key");  // TTL = 100ms
    
    policy.setGlobalTTL(std::chrono::seconds(10));  // Новый TTL
    
    policy.onInsert("new_key");  // TTL = 10s
    
    // old_key должен истечь быстрее
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    
    EXPECT_TRUE(policy.isExpired("old_key"));
    EXPECT_FALSE(policy.isExpired("new_key"));
}

// ==================== onAccess (фиксированный TTL) ====================

TEST(GlobalTTLTest, AccessDoesNotResetTTL) {
    GlobalTTL<std::string> policy(std::chrono::milliseconds(100));
    
    policy.onInsert("key1");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Доступ не должен сбрасывать TTL
    policy.onAccess("key1");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    
    // Должен истечь (50 + 60 = 110 > 100)
    EXPECT_TRUE(policy.isExpired("key1"));
}

// ==================== Различные типы ключей ====================

TEST(GlobalTTLTest, WorksWithIntKeys) {
    GlobalTTL<int> policy(std::chrono::seconds(10));
    
    policy.onInsert(1);
    policy.onInsert(2);
    policy.onInsert(3);
    
    EXPECT_FALSE(policy.isExpired(1));
    EXPECT_FALSE(policy.isExpired(2));
    EXPECT_FALSE(policy.isExpired(3));
    EXPECT_EQ(policy.trackedKeysCount(), 3);
}

// ==================== customTtl игнорируется ====================

TEST(GlobalTTLTest, CustomTtlIgnored) {
    GlobalTTL<std::string> policy(std::chrono::milliseconds(50));
    
    // Передаём customTtl = 10 секунд, но GlobalTTL игнорирует его
    policy.onInsert("key1", std::chrono::seconds(10));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    
    // Должен истечь по глобальному TTL (50ms), а не по custom (10s)
    EXPECT_TRUE(policy.isExpired("key1"));
}