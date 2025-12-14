#pragma once

#include <optional>
#include <cstddef>


/**
 * @brief Базовый интерфейс кэша
 * @tparam K Тип ключа
 * @tparam V Тип значения
 */
template<typename K, typename V>
class ICache {
public:
    virtual ~ICache() = default;

    virtual std::optional<V> get(const K& key) = 0;
    virtual void put(const K& key, const V& value) = 0;
    virtual bool remove(const K& key) = 0;
    virtual void clear() = 0;
    virtual size_t size() const = 0;
    virtual bool contains(const K& key) const = 0;
    virtual size_t capacity() const = 0;
};