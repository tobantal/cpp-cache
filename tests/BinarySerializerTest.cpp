#include <gtest/gtest.h>
#include <cache/serialization/BinarySerializer.hpp>
#include <string>

/**
 * @brief Тесты для BinarySerializer
 * 
 * Проверяем:
 * - Сериализация/десериализация примитивных типов
 * - Сериализация/десериализация std::string
 * - Формат файла (magic, version)
 * - Граничные случаи
 */

// ==================== Базовые операции ====================

TEST(BinarySerializerTest, SerializeDeserializeIntInt) {
    BinarySerializer<int, int> serializer;
    
    auto data = serializer.serialize(42, 100);
    
    int key, value;
    ASSERT_TRUE(serializer.deserialize(data, key, value));
    EXPECT_EQ(key, 42);
    EXPECT_EQ(value, 100);
}

TEST(BinarySerializerTest, SerializeDeserializeStringInt) {
    BinarySerializer<std::string, int> serializer;
    
    auto data = serializer.serialize("hello", 42);
    
    std::string key;
    int value;
    ASSERT_TRUE(serializer.deserialize(data, key, value));
    EXPECT_EQ(key, "hello");
    EXPECT_EQ(value, 42);
}

TEST(BinarySerializerTest, SerializeDeserializeStringString) {
    BinarySerializer<std::string, std::string> serializer;
    
    auto data = serializer.serialize("key", "value");
    
    std::string key, value;
    ASSERT_TRUE(serializer.deserialize(data, key, value));
    EXPECT_EQ(key, "key");
    EXPECT_EQ(value, "value");
}

TEST(BinarySerializerTest, SerializeDeserializeDouble) {
    BinarySerializer<std::string, double> serializer;
    
    auto data = serializer.serialize("pi", 3.14159265359);
    
    std::string key;
    double value;
    ASSERT_TRUE(serializer.deserialize(data, key, value));
    EXPECT_EQ(key, "pi");
    EXPECT_DOUBLE_EQ(value, 3.14159265359);
}

// ==================== SerializeAll / DeserializeAll ====================

TEST(BinarySerializerTest, SerializeAllEmpty) {
    BinarySerializer<std::string, int> serializer;
    
    std::vector<std::pair<std::string, int>> entries;
    auto data = serializer.serializeAll(entries);
    
    // Минимум: magic(4) + version(4) + count(4) = 12 байт
    EXPECT_GE(data.size(), 12u);
    
    auto result = serializer.deserializeAll(data);
    EXPECT_TRUE(result.empty());
}

TEST(BinarySerializerTest, SerializeAllSingleEntry) {
    BinarySerializer<std::string, int> serializer;
    
    std::vector<std::pair<std::string, int>> entries = {{"key1", 42}};
    auto data = serializer.serializeAll(entries);
    
    auto result = serializer.deserializeAll(data);
    
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].first, "key1");
    EXPECT_EQ(result[0].second, 42);
}

TEST(BinarySerializerTest, SerializeAllMultipleEntries) {
    BinarySerializer<std::string, int> serializer;
    
    std::vector<std::pair<std::string, int>> entries = {
        {"alpha", 1},
        {"beta", 2},
        {"gamma", 3}
    };
    
    auto data = serializer.serializeAll(entries);
    auto result = serializer.deserializeAll(data);
    
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0].first, "alpha");
    EXPECT_EQ(result[0].second, 1);
    EXPECT_EQ(result[1].first, "beta");
    EXPECT_EQ(result[1].second, 2);
    EXPECT_EQ(result[2].first, "gamma");
    EXPECT_EQ(result[2].second, 3);
}

TEST(BinarySerializerTest, SerializeAllIntInt) {
    BinarySerializer<int, int> serializer;
    
    std::vector<std::pair<int, int>> entries = {
        {1, 100},
        {2, 200},
        {3, 300}
    };
    
    auto data = serializer.serializeAll(entries);
    auto result = serializer.deserializeAll(data);
    
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], std::make_pair(1, 100));
    EXPECT_EQ(result[1], std::make_pair(2, 200));
    EXPECT_EQ(result[2], std::make_pair(3, 300));
}

// ==================== Формат файла ====================

TEST(BinarySerializerTest, MagicNumber) {
    BinarySerializer<std::string, int> serializer;
    
    std::vector<std::pair<std::string, int>> entries = {{"test", 1}};
    auto data = serializer.serializeAll(entries);
    
    // Проверяем magic "CCHE" (0x45484343 в little-endian)
    ASSERT_GE(data.size(), 4u);
    EXPECT_EQ(data[0], 0x43);  // 'C'
    EXPECT_EQ(data[1], 0x43);  // 'C'
    EXPECT_EQ(data[2], 0x48);  // 'H'
    EXPECT_EQ(data[3], 0x45);  // 'E'
}

TEST(BinarySerializerTest, InvalidMagicThrows) {
    BinarySerializer<std::string, int> serializer;
    
    // Создаём данные с неправильным magic
    std::vector<uint8_t> invalidData = {0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    
    EXPECT_THROW(serializer.deserializeAll(invalidData), std::runtime_error);
}

TEST(BinarySerializerTest, TooSmallDataThrows) {
    BinarySerializer<std::string, int> serializer;
    
    std::vector<uint8_t> tooSmall = {0x43, 0x43, 0x48};  // Только 3 байта
    
    EXPECT_THROW(serializer.deserializeAll(tooSmall), std::runtime_error);
}

// ==================== Граничные случаи ====================

TEST(BinarySerializerTest, EmptyString) {
    BinarySerializer<std::string, std::string> serializer;
    
    auto data = serializer.serialize("", "");
    
    std::string key, value;
    ASSERT_TRUE(serializer.deserialize(data, key, value));
    EXPECT_EQ(key, "");
    EXPECT_EQ(value, "");
}

TEST(BinarySerializerTest, LongString) {
    BinarySerializer<std::string, std::string> serializer;
    
    std::string longKey(1000, 'K');
    std::string longValue(5000, 'V');
    
    auto data = serializer.serialize(longKey, longValue);
    
    std::string key, value;
    ASSERT_TRUE(serializer.deserialize(data, key, value));
    EXPECT_EQ(key, longKey);
    EXPECT_EQ(value, longValue);
}

TEST(BinarySerializerTest, UnicodeString) {
    BinarySerializer<std::string, std::string> serializer;
    
    std::string unicodeKey = "ключ";
    std::string unicodeValue = "значение";
    
    auto data = serializer.serialize(unicodeKey, unicodeValue);
    
    std::string key, value;
    ASSERT_TRUE(serializer.deserialize(data, key, value));
    EXPECT_EQ(key, unicodeKey);
    EXPECT_EQ(value, unicodeValue);
}

TEST(BinarySerializerTest, NegativeNumbers) {
    BinarySerializer<int, int> serializer;
    
    auto data = serializer.serialize(-42, -100);
    
    int key, value;
    ASSERT_TRUE(serializer.deserialize(data, key, value));
    EXPECT_EQ(key, -42);
    EXPECT_EQ(value, -100);
}

TEST(BinarySerializerTest, ZeroValues) {
    BinarySerializer<int, int> serializer;
    
    auto data = serializer.serialize(0, 0);
    
    int key, value;
    ASSERT_TRUE(serializer.deserialize(data, key, value));
    EXPECT_EQ(key, 0);
    EXPECT_EQ(value, 0);
}

TEST(BinarySerializerTest, MaxIntValues) {
    BinarySerializer<int, int> serializer;
    
    int maxInt = std::numeric_limits<int>::max();
    int minInt = std::numeric_limits<int>::min();
    
    auto data = serializer.serialize(maxInt, minInt);
    
    int key, value;
    ASSERT_TRUE(serializer.deserialize(data, key, value));
    EXPECT_EQ(key, maxInt);
    EXPECT_EQ(value, minInt);
}