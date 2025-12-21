#pragma once

#include <cache/serialization/ISerializer.hpp>
#include <cstring>
#include <string>
#include <type_traits>
#include <stdexcept>

/**
 * @brief Бинарный сериализатор для данных кэша
 * @tparam K Тип ключа
 * @tparam V Тип значения
 * 
 * Формат файла:
 * [4 байта: magic "CCHE"]
 * [4 байта: версия формата]
 * [4 байта: количество записей]
 * [записи...]
 * 
 * Формат записи:
 * [4 байта: размер ключа]
 * [N байт: данные ключа]
 * [4 байта: размер значения]
 * [M байт: данные значения]
 * 
 * Поддерживаемые типы:
 * - Примитивные типы (int, double, etc.)
 * - std::string
 * - POD-структуры (через memcpy)
 * 
 * Для сложных типов нужна специализация или другой сериализатор.
 */
template<typename K, typename V>
class BinarySerializer : public ISerializer<K, V> {
public:
    static constexpr uint32_t MAGIC = 0x45484343;  // "CCHE" в little-endian
    static constexpr uint32_t VERSION = 1;
    
    std::vector<uint8_t> serialize(const K& key, const V& value) override {
        std::vector<uint8_t> result;
        
        // Сериализуем ключ
        auto keyData = serializeValue(key);
        appendSize(result, keyData.size());
        result.insert(result.end(), keyData.begin(), keyData.end());
        
        // Сериализуем значение
        auto valueData = serializeValue(value);
        appendSize(result, valueData.size());
        result.insert(result.end(), valueData.begin(), valueData.end());
        
        return result;
    }
    
    bool deserialize(const std::vector<uint8_t>& data, K& key, V& value) override {
        size_t offset = 0;
        
        // Читаем ключ
        if (!deserializeEntry(data, offset, key)) {
            return false;
        }
        
        // Читаем значение
        if (!deserializeEntry(data, offset, value)) {
            return false;
        }
        
        return true;
    }
    
    std::vector<uint8_t> serializeAll(
            const std::vector<std::pair<K, V>>& entries) override {
        std::vector<uint8_t> result;
        
        // Заголовок
        appendUint32(result, MAGIC);
        appendUint32(result, VERSION);
        appendUint32(result, static_cast<uint32_t>(entries.size()));
        
        // Записи
        for (const auto& [key, value] : entries) {
            auto entryData = serialize(key, value);
            result.insert(result.end(), entryData.begin(), entryData.end());
        }
        
        return result;
    }
    
    std::vector<std::pair<K, V>> deserializeAll(
            const std::vector<uint8_t>& data) override {
        std::vector<std::pair<K, V>> result;
        
        if (data.size() < 12) {
            throw std::runtime_error("Invalid cache file: too small");
        }
        
        size_t offset = 0;
        
        // Проверяем magic
        uint32_t magic = readUint32(data, offset);
        if (magic != MAGIC) {
            throw std::runtime_error("Invalid cache file: wrong magic number");
        }
        
        // Проверяем версию
        uint32_t version = readUint32(data, offset);
        if (version != VERSION) {
            throw std::runtime_error("Unsupported cache file version: " + 
                                     std::to_string(version));
        }
        
        // Читаем количество записей
        uint32_t count = readUint32(data, offset);
        result.reserve(count);
        
        // Читаем записи
        for (uint32_t i = 0; i < count; ++i) {
            K key;
            V value;
            
            if (!deserializeEntry(data, offset, key)) {
                throw std::runtime_error("Failed to deserialize key at entry " + 
                                         std::to_string(i));
            }
            
            if (!deserializeEntry(data, offset, value)) {
                throw std::runtime_error("Failed to deserialize value at entry " + 
                                         std::to_string(i));
            }
            
            result.emplace_back(std::move(key), std::move(value));
        }
        
        return result;
    }

private:
    // ==================== Утилиты для записи ====================
    
    void appendUint32(std::vector<uint8_t>& data, uint32_t value) {
        data.push_back(static_cast<uint8_t>(value & 0xFF));
        data.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        data.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        data.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    }
    
    void appendSize(std::vector<uint8_t>& data, size_t size) {
        appendUint32(data, static_cast<uint32_t>(size));
    }
    
    // ==================== Утилиты для чтения ====================
    
    uint32_t readUint32(const std::vector<uint8_t>& data, size_t& offset) {
        if (offset + 4 > data.size()) {
            throw std::runtime_error("Unexpected end of data");
        }
        
        uint32_t value = static_cast<uint32_t>(data[offset]) |
                        (static_cast<uint32_t>(data[offset + 1]) << 8) |
                        (static_cast<uint32_t>(data[offset + 2]) << 16) |
                        (static_cast<uint32_t>(data[offset + 3]) << 24);
        offset += 4;
        return value;
    }
    
    // ==================== Сериализация типов ====================
    
    /**
     * @brief Сериализация для арифметических типов (int, double, etc.)
     */
    template<typename T>
    typename std::enable_if<std::is_arithmetic<T>::value, std::vector<uint8_t>>::type
    serializeValue(const T& value) {
        std::vector<uint8_t> result(sizeof(T));
        std::memcpy(result.data(), &value, sizeof(T));
        return result;
    }
    
    /**
     * @brief Сериализация для std::string
     */
    std::vector<uint8_t> serializeValue(const std::string& value) {
        return std::vector<uint8_t>(value.begin(), value.end());
    }
    
    // ==================== Десериализация типов ====================
    
    /**
     * @brief Десериализация записи (размер + данные)
     */
    template<typename T>
    bool deserializeEntry(const std::vector<uint8_t>& data, size_t& offset, T& value) {
        if (offset + 4 > data.size()) {
            return false;
        }
        
        uint32_t size = readUint32(data, offset);
        
        if (offset + size > data.size()) {
            return false;
        }
        
        std::vector<uint8_t> valueData(data.begin() + offset, 
                                        data.begin() + offset + size);
        offset += size;
        
        return deserializeValue(valueData, value);
    }
    
    /**
     * @brief Десериализация для арифметических типов
     */
    template<typename T>
    typename std::enable_if<std::is_arithmetic<T>::value, bool>::type
    deserializeValue(const std::vector<uint8_t>& data, T& value) {
        if (data.size() != sizeof(T)) {
            return false;
        }
        std::memcpy(&value, data.data(), sizeof(T));
        return true;
    }
    
    /**
     * @brief Десериализация для std::string
     */
    bool deserializeValue(const std::vector<uint8_t>& data, std::string& value) {
        value = std::string(data.begin(), data.end());
        return true;
    }
};