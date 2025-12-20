#pragma once

#include <optional>
#include <cstddef>

/**
 * @brief Базовый интерфейс кэша
 * @tparam K Тип ключа
 * @tparam V Тип значения
 */
template <typename K, typename V>
class ICache
{
public:
    virtual ~ICache() = default;

    /**
     * @brief Получить значение по ключу
     * @param key Ключ
     * @return Значение, если ключ существует, иначе std::nullopt
     */
    virtual std::optional<V> get(const K &key) = 0;

    /**
     * @brief Поместить значение в кэш
     * @param key Ключ
     * @param value Значение
     */
    virtual void put(const K &key, const V &value) = 0;

    /**
     * @brief Удалить значение по ключу
     * @param key Ключ
     * @return true, если элемент был удален, иначе false
     */
    virtual bool remove(const K &key) = 0;

    /**
     * @brief Очистить кэш
     */
    virtual void clear() = 0;

    /**
     * @brief Получить текущий размер кэша
     * @return Размер кэша
     */
    virtual size_t size() const = 0;
    
    /**
     * @brief Проверить наличие ключа в кэше
     * @param key Ключ
     * @return true, если ключ существует, иначе false
     */
    virtual bool contains(const K &key) const = 0;

    /**
     * @brief Получить максимальную емкость кэша
     * @return Емкость кэша
     */
    virtual size_t capacity() const = 0;
};