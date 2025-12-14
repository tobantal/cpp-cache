#include <gtest/gtest.h>
#include "cache/policies/LRUPolicy.hpp"
#include <string>

/**
 * @brief Тесты для LRUPolicy
 * 
 * Проверяем:
 * - Базовые операции (insert, access, remove)
 * - Порядок вытеснения (LRU — последний использованный вытесняется первым)
 * - Корректность после серии операций
 */

// ==================== Базовые операции ====================

TEST(LRUPolicyTest, EmptyOnCreate) {
    LRUPolicy<std::string> policy;
    EXPECT_TRUE(policy.empty());
}

TEST(LRUPolicyTest, NotEmptyAfterInsert) {
    LRUPolicy<std::string> policy;
    policy.onInsert("key1");
    EXPECT_FALSE(policy.empty());
}

TEST(LRUPolicyTest, EmptyAfterClear) {
    LRUPolicy<std::string> policy;
    policy.onInsert("key1");
    policy.onInsert("key2");
    policy.clear();
    EXPECT_TRUE(policy.empty());
}

TEST(LRUPolicyTest, SelectVictimThrowsWhenEmpty) {
    LRUPolicy<std::string> policy;
    EXPECT_THROW(policy.selectVictim(), std::logic_error);
}

// ==================== Порядок вытеснения ====================

TEST(LRUPolicyTest, SelectVictimReturnsOldest) {
    // Вставляем A, B, C — самый старый A
    LRUPolicy<std::string> policy;
    policy.onInsert("A");
    policy.onInsert("B");
    policy.onInsert("C");
    
    EXPECT_EQ(policy.selectVictim(), "A");
}

TEST(LRUPolicyTest, AccessMovesToFront) {
    // Вставляем A, B, C — порядок [C, B, A] (A — LRU)
    // После доступа к A — порядок [A, C, B] (B — LRU)
    LRUPolicy<std::string> policy;
    policy.onInsert("A");
    policy.onInsert("B");
    policy.onInsert("C");
    
    policy.onAccess("A");
    
    EXPECT_EQ(policy.selectVictim(), "B");
}

TEST(LRUPolicyTest, MultipleAccessesChangeOrder) {
    // Вставляем A, B, C, D
    // Доступ к A, потом к B
    // Ожидаемый LRU: C (D новее C, A и B обновлены)
    LRUPolicy<std::string> policy;
    policy.onInsert("A");
    policy.onInsert("B");
    policy.onInsert("C");
    policy.onInsert("D");
    
    policy.onAccess("A");
    policy.onAccess("B");
    
    EXPECT_EQ(policy.selectVictim(), "C");
}

TEST(LRUPolicyTest, RepeatedAccessToSameKey) {
    // Многократный доступ к одному ключу не должен ломать структуру
    LRUPolicy<std::string> policy;
    policy.onInsert("A");
    policy.onInsert("B");
    
    policy.onAccess("A");
    policy.onAccess("A");
    policy.onAccess("A");
    
    EXPECT_EQ(policy.selectVictim(), "B");
}

// ==================== Удаление ====================

TEST(LRUPolicyTest, RemoveUpdatesVictim) {
    // Вставляем A, B, C — LRU это A
    // Удаляем A — теперь LRU это B
    LRUPolicy<std::string> policy;
    policy.onInsert("A");
    policy.onInsert("B");
    policy.onInsert("C");
    
    policy.onRemove("A");
    
    EXPECT_EQ(policy.selectVictim(), "B");
}

TEST(LRUPolicyTest, RemoveMiddleElement) {
    // Удаление элемента из середины списка
    LRUPolicy<std::string> policy;
    policy.onInsert("A");
    policy.onInsert("B");
    policy.onInsert("C");
    
    policy.onRemove("B");
    
    // A всё ещё LRU
    EXPECT_EQ(policy.selectVictim(), "A");
}

TEST(LRUPolicyTest, RemoveNonExistentKeyDoesNothing) {
    // Удаление несуществующего ключа не должно падать
    LRUPolicy<std::string> policy;
    policy.onInsert("A");
    
    policy.onRemove("NonExistent");  // Не должно падать
    
    EXPECT_FALSE(policy.empty());
    EXPECT_EQ(policy.selectVictim(), "A");
}

TEST(LRUPolicyTest, AccessNonExistentKeyDoesNothing) {
    // Доступ к несуществующему ключу не должен падать
    LRUPolicy<std::string> policy;
    policy.onInsert("A");
    
    policy.onAccess("NonExistent");  // Не должно падать
    
    EXPECT_EQ(policy.selectVictim(), "A");
}

// ==================== Граничные случаи ====================

TEST(LRUPolicyTest, SingleElement) {
    LRUPolicy<std::string> policy;
    policy.onInsert("only");
    
    EXPECT_EQ(policy.selectVictim(), "only");
    
    policy.onRemove("only");
    EXPECT_TRUE(policy.empty());
}

TEST(LRUPolicyTest, InsertRemoveInsertSequence) {
    // Проверяем корректность после серии вставок/удалений
    LRUPolicy<int> policy;
    
    policy.onInsert(1);
    policy.onInsert(2);
    policy.onRemove(1);
    policy.onInsert(3);
    
    // После: [3, 2], LRU = 2
    EXPECT_EQ(policy.selectVictim(), 2);
}

TEST(LRUPolicyTest, WorksWithIntKeys) {
    // Проверяем работу с int ключами
    LRUPolicy<int> policy;
    policy.onInsert(100);
    policy.onInsert(200);
    policy.onInsert(300);
    
    policy.onAccess(100);
    
    EXPECT_EQ(policy.selectVictim(), 200);
}