#pragma once

#include <vector>
#include <cstdint>

/**
 * @brief Интерфейс сериализации данных кэша
 * @tparam K Тип ключа
 * @tparam V Тип значения
 * 
 * Отвечает за преобразование данных в байты и обратно.
 * Не знает о файлах/сети — только формат данных.
 * 
 * Реализации:
 * - BinarySerializer — компактный бинарный формат
 * - JsonSerializer — человекочитаемый JSON (будущее)
 */
template<typename K, typename V>
class ISerializer {
public:
    virtual ~ISerializer() = default;
    
    /**
     * @brief Сериализовать пару ключ-значение
     * @param key Ключ
     * @param value Значение
     * @return Байтовое представление
     */
    virtual std::vector<uint8_t> serialize(const K& key, const V& value) = 0;
    
    /**
     * @brief Десериализовать пару ключ-значение
     * @param data Байтовое представление
     * @param[out] key Ключ
     * @param[out] value Значение
     * @return true если десериализация успешна
     */
    virtual bool deserialize(const std::vector<uint8_t>& data, K& key, V& value) = 0;
    
    /**
     * @brief Сериализовать все данные кэша
     * @param entries Вектор пар ключ-значение
     * @return Байтовое представление всех данных
     */
    virtual std::vector<uint8_t> serializeAll(
        const std::vector<std::pair<K, V>>& entries) = 0;
    
    /**
     * @brief Десериализовать все данные кэша
     * @param data Байтовое представление
     * @return Вектор пар ключ-значение
     */
    virtual std::vector<std::pair<K, V>> deserializeAll(
        const std::vector<uint8_t>& data) = 0;
};