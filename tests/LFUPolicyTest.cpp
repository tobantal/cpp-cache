#include <gtest/gtest.h>
#include "cache/eviction/LFUPolicy.hpp"
#include <string>

/**
 * @brief Тесты для LFUPolicy
 * 
 * Проверяем:
 * - Базовые операции (insert, access, remove, clear)
 * - Корректность выбора жертвы по частоте
 * - LRU-порядок при одинаковой частоте (tie-breaker)
 * - Корректность обновления minFrequency
 * - Граничные случаи (1 элемент, удаление минимального)
 * - Работа с разными типами ключей
 */

// ==================== Базовые операции ====================

/**
 * @brief Проверяем начальное состояние политики
 */
TEST(LFUPolicyTest, EmptyOnCreate) {
    LFUPolicy<std::string> policy;
    
    EXPECT_TRUE(policy.empty());
    EXPECT_EQ(policy.getMinFrequency(), 0);
}

/**
 * @brief После вставки политика не пуста
 */
TEST(LFUPolicyTest, NotEmptyAfterInsert) {
    LFUPolicy<std::string> policy;
    
    policy.onInsert("key1");
    
    EXPECT_FALSE(policy.empty());
}

/**
 * @brief Новый ключ получает частоту 1
 */
TEST(LFUPolicyTest, NewKeyHasFrequencyOne) {
    LFUPolicy<std::string> policy;
    
    policy.onInsert("key1");
    
    EXPECT_EQ(policy.getFrequency("key1"), 1);
    EXPECT_EQ(policy.getMinFrequency(), 1);
}

/**
 * @brief clear() сбрасывает политику в начальное состояние
 */
TEST(LFUPolicyTest, ClearResetsPolicy) {
    LFUPolicy<std::string> policy;
    
    policy.onInsert("key1");
    policy.onInsert("key2");
    policy.onAccess("key1");
    
    policy.clear();
    
    EXPECT_TRUE(policy.empty());
    EXPECT_EQ(policy.getMinFrequency(), 0);
    EXPECT_EQ(policy.getFrequency("key1"), 0);  // Ключ больше не отслеживается
}

/**
 * @brief selectVictim() бросает исключение на пустой политике
 */
TEST(LFUPolicyTest, SelectVictimThrowsWhenEmpty) {
    LFUPolicy<std::string> policy;
    
    EXPECT_THROW(policy.selectVictim(), std::logic_error);
}

// ==================== Подсчёт частоты ====================

/**
 * @brief onAccess увеличивает частоту на 1
 */
TEST(LFUPolicyTest, AccessIncreasesFrequency) {
    LFUPolicy<std::string> policy;
    
    policy.onInsert("key1");       // freq = 1
    policy.onAccess("key1");       // freq = 2
    policy.onAccess("key1");       // freq = 3
    
    EXPECT_EQ(policy.getFrequency("key1"), 3);
}

/**
 * @brief Множественные ключи имеют независимые частоты
 */
TEST(LFUPolicyTest, MultipleKeysIndependentFrequencies) {
    LFUPolicy<std::string> policy;
    
    policy.onInsert("A");
    policy.onInsert("B");
    policy.onInsert("C");
    
    policy.onAccess("A");  // A: 2
    policy.onAccess("A");  // A: 3
    policy.onAccess("B");  // B: 2
    
    EXPECT_EQ(policy.getFrequency("A"), 3);
    EXPECT_EQ(policy.getFrequency("B"), 2);
    EXPECT_EQ(policy.getFrequency("C"), 1);
}

/**
 * @brief onAccess на несуществующий ключ не падает и не создаёт ключ
 */
TEST(LFUPolicyTest, AccessNonExistentKeyDoesNothing) {
    LFUPolicy<std::string> policy;
    
    policy.onInsert("key1");
    
    policy.onAccess("nonexistent");  // Не должно падать
    
    EXPECT_EQ(policy.getFrequency("nonexistent"), 0);  // Ключ не создан
    EXPECT_EQ(policy.getFrequency("key1"), 1);         // Существующий не изменён
}

// ==================== Выбор жертвы по частоте ====================

/**
 * @brief selectVictim возвращает ключ с минимальной частотой
 */
TEST(LFUPolicyTest, SelectVictimReturnsLowestFrequency) {
    LFUPolicy<std::string> policy;
    
    policy.onInsert("A");  // freq: 1
    policy.onInsert("B");  // freq: 1
    policy.onInsert("C");  // freq: 1
    
    policy.onAccess("A");  // A: 2
    policy.onAccess("A");  // A: 3
    policy.onAccess("B");  // B: 2
    // C остаётся с частотой 1 — минимальная
    
    EXPECT_EQ(policy.selectVictim(), "C");
}

/**
 * @brief После вытеснения элемента с минимальной частотой, выбирается следующий
 */
TEST(LFUPolicyTest, SelectVictimAfterRemoval) {
    LFUPolicy<std::string> policy;
    
    policy.onInsert("A");  // freq: 1
    policy.onInsert("B");  // freq: 1
    
    policy.onAccess("A");  // A: 2
    // B: 1, A: 2
    
    EXPECT_EQ(policy.selectVictim(), "B");
    
    policy.onRemove("B");
    // Теперь только A с частотой 2
    
    EXPECT_EQ(policy.selectVictim(), "A");
}

/**
 * @brief Сложный сценарий с изменением минимальной частоты
 */
TEST(LFUPolicyTest, MinFrequencyUpdatesCorrectly) {
    LFUPolicy<std::string> policy;
    
    policy.onInsert("A");  // A: 1, minFreq: 1
    policy.onInsert("B");  // B: 1, minFreq: 1
    
    policy.onAccess("A");  // A: 2, B: 1, minFreq: 1
    policy.onAccess("B");  // A: 2, B: 2, minFreq: 2 (список freq=1 пуст)
    
    EXPECT_EQ(policy.getMinFrequency(), 2);
    
    policy.onInsert("C");  // C: 1, minFreq: 1 (новый элемент)
    
    EXPECT_EQ(policy.getMinFrequency(), 1);
    EXPECT_EQ(policy.selectVictim(), "C");
}

// ==================== LRU tie-breaker при равной частоте ====================

/**
 * @brief При равной частоте вытесняется LRU (последний использованный)
 * 
 * Порядок вставки: A, B, C (все с частотой 1)
 * LRU среди них — A (вставлен первым, не использовался)
 */
TEST(LFUPolicyTest, LRUTieBreakerOnEqualFrequency) {
    LFUPolicy<std::string> policy;
    
    policy.onInsert("A");  // A: 1
    policy.onInsert("B");  // B: 1
    policy.onInsert("C");  // C: 1
    // Все с частотой 1, порядок LRU: A (oldest), B, C (newest)
    
    EXPECT_EQ(policy.selectVictim(), "A");
}

/**
 * @brief Access обновляет LRU-позицию внутри группы частоты
 */
TEST(LFUPolicyTest, AccessUpdatesLRUPosition) {
    LFUPolicy<std::string> policy;
    
    policy.onInsert("A");  // A: 1
    policy.onInsert("B");  // B: 1
    policy.onInsert("C");  // C: 1
    // LRU порядок в группе freq=1: A (LRU), B, C (MRU)
    
    // Увеличиваем частоту всех до 2, но в разном порядке
    policy.onAccess("A");  // A: 2
    policy.onAccess("B");  // B: 2
    policy.onAccess("C");  // C: 2
    // Теперь в группе freq=2: A (LRU), B, C (MRU)
    
    EXPECT_EQ(policy.selectVictim(), "A");
    
    // Доступ к A перемещает его в MRU
    policy.onAccess("A");  // A: 3, теперь в группе freq=2: B (LRU), C
    
    EXPECT_EQ(policy.selectVictim(), "B");
}

/**
 * @brief Сложный сценарий: разные частоты + LRU внутри группы
 */
TEST(LFUPolicyTest, ComplexFrequencyAndLRU) {
    LFUPolicy<std::string> policy;
    
    policy.onInsert("A");
    policy.onInsert("B");
    policy.onInsert("C");
    policy.onInsert("D");
    // Все freq=1, LRU: A, B, C, D
    
    policy.onAccess("A");  // A: 2
    policy.onAccess("B");  // B: 2
    // freq=1: C (LRU), D
    // freq=2: A (LRU), B
    
    // Минимальная частота = 1, LRU среди freq=1 = C
    EXPECT_EQ(policy.selectVictim(), "C");
    
    policy.onRemove("C");
    // freq=1: D
    // freq=2: A, B
    
    EXPECT_EQ(policy.selectVictim(), "D");
    
    policy.onRemove("D");
    // freq=2: A (LRU), B
    
    EXPECT_EQ(policy.selectVictim(), "A");
}

// ==================== Удаление ====================

/**
 * @brief onRemove удаляет ключ из политики
 */
TEST(LFUPolicyTest, RemoveDeletesKey) {
    LFUPolicy<std::string> policy;
    
    policy.onInsert("A");
    policy.onInsert("B");
    
    policy.onRemove("A");
    
    EXPECT_EQ(policy.getFrequency("A"), 0);  // Ключ удалён
    EXPECT_EQ(policy.getFrequency("B"), 1);  // B остался
    EXPECT_FALSE(policy.empty());
}

/**
 * @brief onRemove на несуществующий ключ не падает
 */
TEST(LFUPolicyTest, RemoveNonExistentKeyDoesNothing) {
    LFUPolicy<std::string> policy;
    
    policy.onInsert("A");
    
    policy.onRemove("nonexistent");  // Не должно падать
    
    EXPECT_FALSE(policy.empty());
    EXPECT_EQ(policy.getFrequency("A"), 1);
}

/**
 * @brief Удаление единственного элемента делает политику пустой
 */
TEST(LFUPolicyTest, RemoveLastElementMakesEmpty) {
    LFUPolicy<std::string> policy;
    
    policy.onInsert("only");
    policy.onRemove("only");
    
    EXPECT_TRUE(policy.empty());
}

/**
 * @brief Удаление элемента с минимальной частотой корректно обновляет victim
 */
TEST(LFUPolicyTest, RemoveMinFrequencyElementUpdatesVictim) {
    LFUPolicy<std::string> policy;
    
    policy.onInsert("A");  // A: 1
    policy.onInsert("B");  // B: 1
    policy.onAccess("A");  // A: 2
    // B — единственный с freq=1
    
    policy.onRemove("B");
    // Теперь минимальная частота = 2
    
    EXPECT_EQ(policy.selectVictim(), "A");
}

// ==================== Граничные случаи ====================

/**
 * @brief Работа с одним элементом
 */
TEST(LFUPolicyTest, SingleElement) {
    LFUPolicy<std::string> policy;
    
    policy.onInsert("only");
    
    EXPECT_EQ(policy.selectVictim(), "only");
    EXPECT_EQ(policy.getFrequency("only"), 1);
    
    policy.onAccess("only");
    policy.onAccess("only");
    
    EXPECT_EQ(policy.getFrequency("only"), 3);
    EXPECT_EQ(policy.selectVictim(), "only");  // Всё ещё единственный
    
    policy.onRemove("only");
    
    EXPECT_TRUE(policy.empty());
}

/**
 * @brief Высокие частоты обращений
 */
TEST(LFUPolicyTest, HighFrequencies) {
    LFUPolicy<int> policy;
    
    policy.onInsert(1);
    
    // Много обращений к ключу 1
    for (int i = 0; i < 1000; ++i) {
        policy.onAccess(1);
    }
    
    EXPECT_EQ(policy.getFrequency(1), 1001);  // 1 (insert) + 1000 (access)
    
    policy.onInsert(2);  // freq = 1
    
    // Ключ 2 должен быть жертвой (freq=1 < freq=1001)
    EXPECT_EQ(policy.selectVictim(), 2);
}

/**
 * @brief Последовательность insert-remove-insert
 */
TEST(LFUPolicyTest, InsertRemoveInsertSequence) {
    LFUPolicy<int> policy;
    
    policy.onInsert(1);
    policy.onInsert(2);
    policy.onRemove(1);
    policy.onInsert(3);
    
    // После: 2 (freq=1, LRU), 3 (freq=1, MRU)
    EXPECT_EQ(policy.selectVictim(), 2);
}

/**
 * @brief Все элементы с одинаковой частотой
 */
TEST(LFUPolicyTest, AllSameFrequency) {
    LFUPolicy<std::string> policy;
    
    policy.onInsert("A");
    policy.onInsert("B");
    policy.onInsert("C");
    
    // Все с частотой 1, LRU = A
    EXPECT_EQ(policy.selectVictim(), "A");
    
    // Увеличиваем всех до 2
    policy.onAccess("A");
    policy.onAccess("B");
    policy.onAccess("C");
    
    // Все с частотой 2, LRU = A (первый получил access)
    EXPECT_EQ(policy.selectVictim(), "A");
}

// ==================== Работа с разными типами ключей ====================

/**
 * @brief Работа с int ключами
 */
TEST(LFUPolicyTest, WorksWithIntKeys) {
    LFUPolicy<int> policy;
    
    policy.onInsert(100);
    policy.onInsert(200);
    policy.onInsert(300);
    
    policy.onAccess(100);
    policy.onAccess(100);
    
    EXPECT_EQ(policy.getFrequency(100), 3);
    EXPECT_EQ(policy.getFrequency(200), 1);
    EXPECT_EQ(policy.selectVictim(), 200);  // или 300, оба freq=1
}

/**
 * @brief Работа с длинными строками
 */
TEST(LFUPolicyTest, WorksWithLongStrings) {
    LFUPolicy<std::string> policy;
    
    std::string longKey1 = "this_is_a_very_long_key_for_testing_purposes_1";
    std::string longKey2 = "this_is_a_very_long_key_for_testing_purposes_2";
    
    policy.onInsert(longKey1);
    policy.onInsert(longKey2);
    policy.onAccess(longKey1);
    
    EXPECT_EQ(policy.getFrequency(longKey1), 2);
    EXPECT_EQ(policy.selectVictim(), longKey2);
}

// ==================== Интеграция с Cache ====================

/**
 * @brief Симуляция реального использования с Cache
 * 
 * Проверяем типичный сценарий:
 * 1. Вставка элементов
 * 2. Доступ к "горячим" элементам
 * 3. Вытеснение "холодных"
 */
TEST(LFUPolicyTest, SimulateCacheUsage) {
    LFUPolicy<std::string> policy;
    
    // Фаза 1: Начальная загрузка
    policy.onInsert("user:1");
    policy.onInsert("user:2");
    policy.onInsert("user:3");
    policy.onInsert("product:1");
    policy.onInsert("product:2");
    
    // Фаза 2: "Горячие" данные — часто запрашиваются
    for (int i = 0; i < 10; ++i) {
        policy.onAccess("user:1");    // Популярный пользователь
        policy.onAccess("product:1"); // Популярный продукт
    }
    
    // user:1 и product:1 имеют высокую частоту
    EXPECT_EQ(policy.getFrequency("user:1"), 11);     // 1 + 10
    EXPECT_EQ(policy.getFrequency("product:1"), 11);  // 1 + 10
    
    // "Холодные" данные — редко запрашиваются
    EXPECT_EQ(policy.getFrequency("user:2"), 1);
    EXPECT_EQ(policy.getFrequency("user:3"), 1);
    EXPECT_EQ(policy.getFrequency("product:2"), 1);
    
    // Фаза 3: Вытеснение — должны вытесняться "холодные"
    // Жертва — один из элементов с freq=1, LRU среди них = user:2
    EXPECT_EQ(policy.selectVictim(), "user:2");
    
    policy.onRemove("user:2");
    
    // Следующая жертва — user:3 (следующий LRU среди freq=1)
    EXPECT_EQ(policy.selectVictim(), "user:3");
}

// ==================== Стресс-тесты ====================

/**
 * @brief Много вставок и удалений
 */
TEST(LFUPolicyTest, ManyInsertionsAndRemovals) {
    LFUPolicy<int> policy;
    
    // Вставляем 1000 элементов
    for (int i = 0; i < 1000; ++i) {
        policy.onInsert(i);
    }
    
    EXPECT_FALSE(policy.empty());
    
    // Удаляем половину
    for (int i = 0; i < 500; ++i) {
        policy.onRemove(i);
    }
    
    // Должны остаться элементы 500-999
    EXPECT_EQ(policy.getFrequency(499), 0);  // Удалён
    EXPECT_EQ(policy.getFrequency(500), 1);  // Остался
    
    // selectVictim должен вернуть один из оставшихся (LRU = 500)
    EXPECT_EQ(policy.selectVictim(), 500);
}

/**
 * @brief Много обращений к случайным ключам
 */
TEST(LFUPolicyTest, ManyRandomAccesses) {
    LFUPolicy<int> policy;
    
    // Вставляем 100 элементов
    for (int i = 0; i < 100; ++i) {
        policy.onInsert(i);
    }
    
    // Много обращений к первым 10 элементам
    for (int round = 0; round < 50; ++round) {
        for (int i = 0; i < 10; ++i) {
            policy.onAccess(i);
        }
    }
    
    // Первые 10 элементов имеют высокую частоту
    EXPECT_EQ(policy.getFrequency(0), 51);   // 1 + 50
    EXPECT_EQ(policy.getFrequency(9), 51);   // 1 + 50
    EXPECT_EQ(policy.getFrequency(10), 1);   // Не использовался
    EXPECT_EQ(policy.getFrequency(99), 1);   // Не использовался
    
    // Жертва — один из "холодных" элементов
    int victim = policy.selectVictim();
    EXPECT_GE(victim, 10);  // Должен быть из диапазона 10-99
    EXPECT_LE(victim, 99);
}