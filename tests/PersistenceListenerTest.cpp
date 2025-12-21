#include <gtest/gtest.h>
#include <cache/Cache.hpp>
#include <cache/eviction/LRUPolicy.hpp>
#include <cache/listeners/PersistenceListener.hpp>
#include <cache/persistence/SnapshotPersistence.hpp>
#include <cache/serialization/BinarySerializer.hpp>
#include <filesystem>
#include <string>

/**
 * @brief Тесты для PersistenceListener
 * 
 * Проверяем:
 * - Интеграция с Cache
 * - Сохранение при операциях put/remove/clear
 * - Загрузка данных
 * - Работа с вытеснением
 */

class PersistenceListenerTest : public ::testing::Test {
protected:
    void SetUp() override {
        testFile_ = "/tmp/cache_listener_test_" + std::to_string(std::rand()) + ".bin";
        serializer_ = std::make_shared<BinarySerializer<std::string, int>>();
    }
    
    void TearDown() override {
        std::filesystem::remove(testFile_);
        std::filesystem::remove(testFile_ + ".tmp");
    }
    
    Cache<std::string, int> makeCache(size_t capacity) {
        return Cache<std::string, int>(capacity, 
            std::make_unique<LRUPolicy<std::string>>());
    }
    
    std::string testFile_;
    std::shared_ptr<BinarySerializer<std::string, int>> serializer_;
};

// ==================== Конструктор ====================

TEST_F(PersistenceListenerTest, ConstructorThrowsOnNull) {
    EXPECT_THROW(
        (PersistenceListener<std::string, int>(nullptr)),
        std::invalid_argument
    );
}

// ==================== Интеграция с Cache ====================

TEST_F(PersistenceListenerTest, PutTriggersSave) {
    auto persistence = std::make_shared<SnapshotPersistence<std::string, int>>(
        testFile_, serializer_, true);  // autoFlush = true
    auto listener = std::make_shared<PersistenceListener<std::string, int>>(persistence);
    
    auto cache = makeCache(10);
    cache.addListener(listener);
    
    cache.put("key1", 42);
    
    // Проверяем, что данные сохранились
    auto loaded = persistence->load();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].first, "key1");
    EXPECT_EQ(loaded[0].second, 42);
}

TEST_F(PersistenceListenerTest, UpdateTriggersSave) {
    auto persistence = std::make_shared<SnapshotPersistence<std::string, int>>(
        testFile_, serializer_, true);
    auto listener = std::make_shared<PersistenceListener<std::string, int>>(persistence);
    
    auto cache = makeCache(10);
    cache.addListener(listener);
    
    cache.put("key1", 42);
    cache.put("key1", 100);  // Update
    
    auto loaded = persistence->load();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].second, 100);
}

TEST_F(PersistenceListenerTest, RemoveTriggersSave) {
    auto persistence = std::make_shared<SnapshotPersistence<std::string, int>>(
        testFile_, serializer_, true);
    auto listener = std::make_shared<PersistenceListener<std::string, int>>(persistence);
    
    auto cache = makeCache(10);
    cache.addListener(listener);
    
    cache.put("key1", 42);
    cache.put("key2", 100);
    cache.remove("key1");
    
    auto loaded = persistence->load();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].first, "key2");
}

TEST_F(PersistenceListenerTest, ClearTriggersSave) {
    auto persistence = std::make_shared<SnapshotPersistence<std::string, int>>(
        testFile_, serializer_, true);
    auto listener = std::make_shared<PersistenceListener<std::string, int>>(persistence);
    
    auto cache = makeCache(10);
    cache.addListener(listener);
    
    cache.put("key1", 42);
    cache.put("key2", 100);
    cache.clear();
    
    auto loaded = persistence->load();
    EXPECT_TRUE(loaded.empty());
}

TEST_F(PersistenceListenerTest, EvictionTriggersSave) {
    auto persistence = std::make_shared<SnapshotPersistence<std::string, int>>(
        testFile_, serializer_, true);
    auto listener = std::make_shared<PersistenceListener<std::string, int>>(persistence);
    
    auto cache = makeCache(2);  // Маленький кэш
    cache.addListener(listener);
    
    cache.put("A", 1);
    cache.put("B", 2);
    cache.put("C", 3);  // Вытеснит A
    
    auto loaded = persistence->load();
    ASSERT_EQ(loaded.size(), 2u);
    
    // Проверяем, что A вытеснен
    bool hasA = false;
    for (const auto& entry : loaded) {
        if (entry.first == "A") hasA = true;
    }
    EXPECT_FALSE(hasA);
}

// ==================== Загрузка данных ====================

TEST_F(PersistenceListenerTest, LoadBeforeAddingListener) {
    // Сохраняем данные
    {
        auto persistence = std::make_shared<SnapshotPersistence<std::string, int>>(
            testFile_, serializer_);
        persistence->saveAll({{"saved1", 1}, {"saved2", 2}});
    }
    
    // Загружаем в новый кэш
    auto persistence = std::make_shared<SnapshotPersistence<std::string, int>>(
        testFile_, serializer_, true);
    
    // Сначала загружаем данные
    auto savedData = persistence->load();
    
    auto cache = makeCache(10);
    
    // Загружаем в кэш ДО добавления слушателя
    for (const auto& [key, value] : savedData) {
        cache.put(key, value);
    }
    
    // Теперь добавляем слушатель
    auto listener = std::make_shared<PersistenceListener<std::string, int>>(persistence);
    cache.addListener(listener);
    
    // Проверяем, что данные в кэше
    EXPECT_EQ(cache.get("saved1").value(), 1);
    EXPECT_EQ(cache.get("saved2").value(), 2);
    
    // Добавляем новый элемент
    cache.put("new", 3);
    
    // Проверяем, что все данные сохранены
    auto loaded = persistence->load();
    EXPECT_EQ(loaded.size(), 3u);
}

// ==================== Flush ====================

TEST_F(PersistenceListenerTest, ManualFlush) {
    auto persistence = std::make_shared<SnapshotPersistence<std::string, int>>(
        testFile_, serializer_, false);  // autoFlush = false
    auto listener = std::make_shared<PersistenceListener<std::string, int>>(persistence);
    
    auto cache = makeCache(10);
    cache.addListener(listener);
    
    cache.put("key1", 42);
    
    // Данные ещё не сохранены
    EXPECT_FALSE(persistence->exists());
    
    listener->flush();
    
    // Теперь сохранены
    EXPECT_TRUE(persistence->exists());
    auto loaded = persistence->load();
    ASSERT_EQ(loaded.size(), 1u);
}

// ==================== Доступ к persistence ====================

TEST_F(PersistenceListenerTest, PersistenceAccessor) {
    auto persistence = std::make_shared<SnapshotPersistence<std::string, int>>(
        testFile_, serializer_);
    auto listener = std::make_shared<PersistenceListener<std::string, int>>(persistence);
    
    EXPECT_EQ(listener->persistence(), persistence);
}

// ==================== Hit и Miss не вызывают сохранение ====================

TEST_F(PersistenceListenerTest, HitDoesNotTriggerSave) {
    auto persistence = std::make_shared<SnapshotPersistence<std::string, int>>(
        testFile_, serializer_, true);
    auto listener = std::make_shared<PersistenceListener<std::string, int>>(persistence);
    
    auto cache = makeCache(10);
    cache.addListener(listener);
    
    cache.put("key1", 42);
    
    // Запоминаем размер файла после put
    auto sizeAfterPut = std::filesystem::file_size(testFile_);
    
    // Hit не должен менять файл
    cache.get("key1");
    cache.get("key1");
    cache.get("key1");
    
    auto sizeAfterGets = std::filesystem::file_size(testFile_);
    
    // Размер файла не изменился (или изменился только из-за timestamp)
    // В нашем случае — не должен меняться вообще
    EXPECT_EQ(sizeAfterPut, sizeAfterGets);
}

// ==================== Полный цикл ====================

TEST_F(PersistenceListenerTest, FullCycle) {
    // 1. Создаём кэш, работаем с ним, закрываем
    {
        auto persistence = std::make_shared<SnapshotPersistence<std::string, int>>(
            testFile_, serializer_, false);
        auto listener = std::make_shared<PersistenceListener<std::string, int>>(persistence);
        
        auto cache = makeCache(10);
        cache.addListener(listener);
        
        cache.put("user:1", 100);
        cache.put("user:2", 200);
        cache.put("user:3", 300);
        cache.remove("user:2");
        
        listener->flush();
    }
    
    // 2. Открываем заново, загружаем данные
    {
        auto persistence = std::make_shared<SnapshotPersistence<std::string, int>>(
            testFile_, serializer_, true);
        
        auto savedData = persistence->load();
        
        auto cache = makeCache(10);
        for (const auto& [key, value] : savedData) {
            cache.put(key, value);
        }
        
        auto listener = std::make_shared<PersistenceListener<std::string, int>>(persistence);
        cache.addListener(listener);
        
        // Проверяем восстановленные данные
        EXPECT_TRUE(cache.contains("user:1"));
        EXPECT_TRUE(cache.contains("user:3"));
        EXPECT_FALSE(cache.contains("user:2"));  // Был удалён
        
        EXPECT_EQ(cache.get("user:1").value(), 100);
        EXPECT_EQ(cache.get("user:3").value(), 300);
    }
}