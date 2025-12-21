#include <gtest/gtest.h>
#include <cache/persistence/SnapshotPersistence.hpp>
#include <cache/serialization/BinarySerializer.hpp>
#include <filesystem>
#include <string>

/**
 * @brief Тесты для SnapshotPersistence
 * 
 * Проверяем:
 * - Сохранение и загрузка данных
 * - Инкрементальные операции (onPut, onRemove, onClear)
 * - Режимы autoFlush
 * - Атомарность записи
 * - Работа с несуществующим файлом
 */

class SnapshotPersistenceTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Уникальное имя файла для каждого теста
        testFile_ = "/tmp/cache_test_" + std::to_string(std::rand()) + ".bin";
        serializer_ = std::make_shared<BinarySerializer<std::string, int>>();
    }
    
    void TearDown() override {
        // Удаляем тестовый файл
        std::filesystem::remove(testFile_);
        std::filesystem::remove(testFile_ + ".tmp");
    }
    
    std::string testFile_;
    std::shared_ptr<BinarySerializer<std::string, int>> serializer_;
};

// ==================== Базовые операции ====================

TEST_F(SnapshotPersistenceTest, ConstructorThrowsOnNullSerializer) {
    EXPECT_THROW(
        (SnapshotPersistence<std::string, int>(testFile_, nullptr)),
        std::invalid_argument
    );
}

TEST_F(SnapshotPersistenceTest, ExistsReturnsFalseForNewFile) {
    SnapshotPersistence<std::string, int> persistence(testFile_, serializer_);
    
    EXPECT_FALSE(persistence.exists());
}

TEST_F(SnapshotPersistenceTest, LoadReturnsEmptyForNonExistentFile) {
    SnapshotPersistence<std::string, int> persistence(testFile_, serializer_);
    
    auto data = persistence.load();
    
    EXPECT_TRUE(data.empty());
}

TEST_F(SnapshotPersistenceTest, SaveAllAndLoad) {
    SnapshotPersistence<std::string, int> persistence(testFile_, serializer_);
    
    std::vector<std::pair<std::string, int>> entries = {
        {"alpha", 1},
        {"beta", 2},
        {"gamma", 3}
    };
    
    persistence.saveAll(entries);
    
    EXPECT_TRUE(persistence.exists());
    
    auto loaded = persistence.load();
    
    ASSERT_EQ(loaded.size(), 3u);
    EXPECT_EQ(loaded[0].first, "alpha");
    EXPECT_EQ(loaded[0].second, 1);
    EXPECT_EQ(loaded[1].first, "beta");
    EXPECT_EQ(loaded[1].second, 2);
    EXPECT_EQ(loaded[2].first, "gamma");
    EXPECT_EQ(loaded[2].second, 3);
}

TEST_F(SnapshotPersistenceTest, SaveAllOverwrites) {
    SnapshotPersistence<std::string, int> persistence(testFile_, serializer_);
    
    persistence.saveAll({{"old", 1}});
    persistence.saveAll({{"new", 2}});
    
    auto loaded = persistence.load();
    
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].first, "new");
    EXPECT_EQ(loaded[0].second, 2);
}

// ==================== Инкрементальные операции ====================

TEST_F(SnapshotPersistenceTest, OnPutAddsEntry) {
    SnapshotPersistence<std::string, int> persistence(testFile_, serializer_, true);
    
    persistence.onPut("key1", 100);
    
    auto loaded = persistence.load();
    
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].first, "key1");
    EXPECT_EQ(loaded[0].second, 100);
}

TEST_F(SnapshotPersistenceTest, OnPutUpdatesEntry) {
    SnapshotPersistence<std::string, int> persistence(testFile_, serializer_, true);
    
    persistence.onPut("key1", 100);
    persistence.onPut("key1", 200);  // Update
    
    auto loaded = persistence.load();
    
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].second, 200);
}

TEST_F(SnapshotPersistenceTest, OnRemoveDeletesEntry) {
    SnapshotPersistence<std::string, int> persistence(testFile_, serializer_, true);
    
    persistence.onPut("key1", 100);
    persistence.onPut("key2", 200);
    persistence.onRemove("key1");
    
    auto loaded = persistence.load();
    
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].first, "key2");
}

TEST_F(SnapshotPersistenceTest, OnRemoveNonExistentDoesNothing) {
    SnapshotPersistence<std::string, int> persistence(testFile_, serializer_, true);
    
    persistence.onPut("key1", 100);
    persistence.onRemove("nonexistent");  // Не должен падать
    
    auto loaded = persistence.load();
    
    ASSERT_EQ(loaded.size(), 1u);
}

TEST_F(SnapshotPersistenceTest, OnClearRemovesAll) {
    SnapshotPersistence<std::string, int> persistence(testFile_, serializer_, true);
    
    persistence.onPut("key1", 100);
    persistence.onPut("key2", 200);
    persistence.onClear();
    
    auto loaded = persistence.load();
    
    EXPECT_TRUE(loaded.empty());
}

// ==================== AutoFlush ====================

TEST_F(SnapshotPersistenceTest, AutoFlushDisabledRequiresExplicitFlush) {
    SnapshotPersistence<std::string, int> persistence(testFile_, serializer_, false);
    
    persistence.onPut("key1", 100);
    
    // Файл ещё не создан
    EXPECT_FALSE(persistence.exists());
    EXPECT_TRUE(persistence.isDirty());
    
    persistence.flush();
    
    EXPECT_TRUE(persistence.exists());
    EXPECT_FALSE(persistence.isDirty());
    
    auto loaded = persistence.load();
    ASSERT_EQ(loaded.size(), 1u);
}

TEST_F(SnapshotPersistenceTest, AutoFlushEnabledWritesImmediately) {
    SnapshotPersistence<std::string, int> persistence(testFile_, serializer_, true);
    
    persistence.onPut("key1", 100);
    
    // Файл уже создан
    EXPECT_TRUE(persistence.exists());
    EXPECT_FALSE(persistence.isDirty());
}

TEST_F(SnapshotPersistenceTest, FlushWhenNotDirtyDoesNothing) {
    SnapshotPersistence<std::string, int> persistence(testFile_, serializer_, false);
    
    persistence.flush();  // Не должен падать
    
    EXPECT_FALSE(persistence.exists());
}

// ==================== Persistence между сессиями ====================

TEST_F(SnapshotPersistenceTest, DataPersistsBetweenInstances) {
    // Первый экземпляр — сохраняем
    {
        SnapshotPersistence<std::string, int> persistence(testFile_, serializer_);
        persistence.saveAll({{"persistent", 42}});
    }
    
    // Второй экземпляр — загружаем
    {
        SnapshotPersistence<std::string, int> persistence(testFile_, serializer_);
        auto loaded = persistence.load();
        
        ASSERT_EQ(loaded.size(), 1u);
        EXPECT_EQ(loaded[0].first, "persistent");
        EXPECT_EQ(loaded[0].second, 42);
    }
}

// ==================== Граничные случаи ====================

TEST_F(SnapshotPersistenceTest, EmptySaveAll) {
    SnapshotPersistence<std::string, int> persistence(testFile_, serializer_);
    
    persistence.saveAll({});
    
    EXPECT_TRUE(persistence.exists());
    
    auto loaded = persistence.load();
    EXPECT_TRUE(loaded.empty());
}

TEST_F(SnapshotPersistenceTest, LargeDataSet) {
    SnapshotPersistence<std::string, int> persistence(testFile_, serializer_);
    
    std::vector<std::pair<std::string, int>> entries;
    for (int i = 0; i < 1000; ++i) {
        entries.emplace_back("key" + std::to_string(i), i);
    }
    
    persistence.saveAll(entries);
    auto loaded = persistence.load();
    
    ASSERT_EQ(loaded.size(), 1000u);
    
    for (int i = 0; i < 1000; ++i) {
        EXPECT_EQ(loaded[i].first, "key" + std::to_string(i));
        EXPECT_EQ(loaded[i].second, i);
    }
}

TEST_F(SnapshotPersistenceTest, FilePath) {
    SnapshotPersistence<std::string, int> persistence(testFile_, serializer_);
    
    EXPECT_EQ(persistence.filePath(), testFile_);
}

// ==================== Типы данных ====================

TEST(SnapshotPersistenceTypesTest, IntIntTypes) {
    std::string testFile = "/tmp/cache_int_test_" + std::to_string(std::rand()) + ".bin";
    auto serializer = std::make_shared<BinarySerializer<int, int>>();
    
    SnapshotPersistence<int, int> persistence(testFile, serializer);
    
    persistence.saveAll({{1, 100}, {2, 200}, {3, 300}});
    auto loaded = persistence.load();
    
    ASSERT_EQ(loaded.size(), 3u);
    EXPECT_EQ(loaded[0], std::make_pair(1, 100));
    EXPECT_EQ(loaded[1], std::make_pair(2, 200));
    EXPECT_EQ(loaded[2], std::make_pair(3, 300));
    
    std::filesystem::remove(testFile);
}

TEST(SnapshotPersistenceTypesTest, StringStringTypes) {
    std::string testFile = "/tmp/cache_str_test_" + std::to_string(std::rand()) + ".bin";
    auto serializer = std::make_shared<BinarySerializer<std::string, std::string>>();
    
    SnapshotPersistence<std::string, std::string> persistence(testFile, serializer);
    
    persistence.saveAll({{"key1", "value1"}, {"key2", "value2"}});
    auto loaded = persistence.load();
    
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[0].first, "key1");
    EXPECT_EQ(loaded[0].second, "value1");
    
    std::filesystem::remove(testFile);
}