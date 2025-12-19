#include <gtest/gtest.h>
#include <cache/expiration/NoExpiration.hpp>

/**
 * @brief Тесты для NoExpiration
 * 
 * Проверяем, что NoExpiration — безопасная заглушка:
 * - Ничего не истекает
 * - Все операции безопасны
 * - timeToLive всегда nullopt
 */

// ==================== isExpired ====================

TEST(NoExpirationTest, NeverExpired) {
    NoExpiration<std::string> policy;
    
    policy.onInsert("key1");
    
    EXPECT_FALSE(policy.isExpired("key1"));
}

TEST(NoExpirationTest, UnknownKeyNotExpired) {
    NoExpiration<std::string> policy;
    
    EXPECT_FALSE(policy.isExpired("unknown"));
}

// ==================== Операции не падают ====================

TEST(NoExpirationTest, InsertDoesNotThrow) {
    NoExpiration<std::string> policy;
    
    EXPECT_NO_THROW(policy.onInsert("key1"));
    EXPECT_NO_THROW(policy.onInsert("key2", std::chrono::seconds(10)));
}

TEST(NoExpirationTest, AccessDoesNotThrow) {
    NoExpiration<std::string> policy;
    
    EXPECT_NO_THROW(policy.onAccess("key1"));
    EXPECT_NO_THROW(policy.onAccess("unknown"));
}

TEST(NoExpirationTest, RemoveDoesNotThrow) {
    NoExpiration<std::string> policy;
    
    EXPECT_NO_THROW(policy.onRemove("key1"));
    EXPECT_NO_THROW(policy.onRemove("unknown"));
}

TEST(NoExpirationTest, ClearDoesNotThrow) {
    NoExpiration<std::string> policy;
    
    policy.onInsert("key1");
    policy.onInsert("key2");
    
    EXPECT_NO_THROW(policy.clear());
}

// ==================== timeToLive ====================

TEST(NoExpirationTest, TimeToLiveAlwaysNullopt) {
    NoExpiration<std::string> policy;
    
    policy.onInsert("key1");
    
    auto ttl = policy.timeToLive("key1");
    
    EXPECT_FALSE(ttl.has_value());  // Бесконечный TTL
}

TEST(NoExpirationTest, TimeToLiveUnknownKeyNullopt) {
    NoExpiration<std::string> policy;
    
    auto ttl = policy.timeToLive("unknown");
    
    EXPECT_FALSE(ttl.has_value());
}

// ==================== collectExpired ====================

TEST(NoExpirationTest, CollectExpiredAlwaysEmpty) {
    NoExpiration<std::string> policy;
    
    policy.onInsert("key1");
    policy.onInsert("key2");
    policy.onInsert("key3");
    
    auto expired = policy.collectExpired();
    
    EXPECT_TRUE(expired.empty());
}

// ==================== Различные типы ключей ====================

TEST(NoExpirationTest, WorksWithIntKeys) {
    NoExpiration<int> policy;
    
    policy.onInsert(1);
    policy.onInsert(2);
    
    EXPECT_FALSE(policy.isExpired(1));
    EXPECT_FALSE(policy.isExpired(2));
}

TEST(NoExpirationTest, WorksWithLongKeys) {
    NoExpiration<long long> policy;
    
    policy.onInsert(1234567890123LL);
    
    EXPECT_FALSE(policy.isExpired(1234567890123LL));
}